#include "game.h"
#include "character.h"
#include "raymath.h"
#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// Real-time 3D battle. You fight alongside your troops in third person.
//   WASD move, mouse look, SPACE jump.
//   LMB attack — the swing DIRECTION follows your mouse motion (Mount & Blade
//   style): flick up = overhead, down = thrust, left/right = side cuts.
//   RMB block.
// The battle ends when one side is wiped out, or you fall. Casualties and the
// outcome carry back to the campaign map.
// ---------------------------------------------------------------------------

namespace {

constexpr float ARENA          = 90.0f;   // half-size of battlefield
constexpr float SOLDIER_RADIUS = 0.6f;
constexpr float ATTACK_COOLDOWN = 1.1f;   // placeholder; per-weapon later
constexpr float HIT_DAMAGE      = 25.0f;  // placeholder; per-weapon later

struct Soldier {
    Vector3 pos;
    int     troop;        // troop handle
    Team    team;
    float   hp;
    float   maxHp;
    float   yaw = 0;
    float   cooldown = 0;
    float   swing = 0;
    float   walkPhase = 0;
    int     target = -1;
};

struct BattleState {
    std::vector<Soldier> soldiers;

    // Player avatar
    Vector3 pPos;
    float   pHp = 0, pMaxHp = 0;
    float   yaw = 0, pitch = 0;
    float   cooldown = 0;
    float   swing = 0;
    AttackDir attackDir = AttackDir::Right;
    bool    blocking = false;
    float   vY = 0;
    float   walkPhase = 0;

    bool  over = false;
    bool  won = false;
    float overTimer = 0;

    std::vector<int> startAllies;   // parallel to troops

    // Manual mouse tracking (GetMouseDelta unreliable under WSL/X11).
    Vector2 lastMouse{ 0, 0 };
    bool    hasLastMouse = false;
    Vector2 aimAccum{ 0, 0 };       // recent mouse motion, for attack direction
};

BattleState B;

// Pick an attack direction from accumulated mouse motion.
AttackDir DirFromMotion(Vector2 m) {
    if (fabsf(m.x) > fabsf(m.y))
        return m.x > 0 ? AttackDir::Right : AttackDir::Left;
    return m.y < 0 ? AttackDir::Up : AttackDir::Down;
}

const Loadout& TroopLoadout(const GameState& gs, int troop) {
    return gs.content.troops[troop].loadout;
}

void SpawnLine(GameState& gs, Team team, const std::vector<int>& counts, float zBase) {
    int n = 0;
    for (int troop = (int)counts.size() - 1; troop >= 0; --troop) {
        for (int i = 0; i < counts[troop]; ++i) {
            Soldier s;
            s.troop = troop;
            s.team = team;
            s.maxHp = (float)gs.content.troops[troop].maxHp;
            s.hp = s.maxHp;
            const float x = -20.0f + (n % 10) * 4.0f;
            const float z = zBase + (n / 10) * 4.0f * (team == Team::Enemy ? 1.0f : -1.0f);
            s.pos = { x, 0, z };
            s.yaw = (team == Team::Enemy) ? PI : 0.0f;
            B.soldiers.push_back(s);
            ++n;
        }
    }
}

int FindNearest(const Soldier& me, Team wantTeam) {
    int best = -1;
    float bestD = 1e9f;
    for (int i = 0; i < (int)B.soldiers.size(); ++i) {
        const Soldier& o = B.soldiers[i];
        if (o.hp <= 0 || o.team != wantTeam) continue;
        const float d = Vector3DistanceSqr(me.pos, o.pos);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

Color TeamTint(Team t) { return t == Team::Enemy ? RED : BLUE; }

void EndBattle(GameState& gs, bool won) {
    B.over = true;
    B.won = won;
    B.overTimer = 2.5f;

    std::vector<int> survivors(gs.content.troops.size(), 0);
    for (const Soldier& s : B.soldiers)
        if (s.team == Team::Player && s.hp > 0) survivors[s.troop]++;
    for (int t = 0; t < (int)gs.playerLosses.size(); ++t)
        gs.playerLosses[t] = B.startAllies[t] - survivors[t];
    gs.battleWon = won;
    EnableCursor();
}

}  // namespace

void BattleInit(GameState& gs) {
    B = BattleState{};
    const Party& enemy = gs.parties[gs.battlePartyIndex];

    SpawnLine(gs, Team::Player, gs.player.troopCounts, -30.0f);
    SpawnLine(gs, Team::Enemy,  enemy.troopCounts,      30.0f);

    B.startAllies = gs.player.troopCounts;
    gs.playerLosses.assign(gs.content.troops.size(), 0);

    B.pPos = { 0, 0, -38 };
    B.pMaxHp = (float)gs.playerHero.maxHp;
    B.pHp = B.pMaxHp;

    DisableCursor();
    B.hasLastMouse = false;
}

void BattleUpdateDraw(GameState& gs, float dt) {
    if (dt > 0.05f) dt = 0.05f;
    const Content& c = gs.content;

    // ---------- player input ----------
    Vector3 fwd = { sinf(B.yaw), 0, cosf(B.yaw) };
    if (!B.over) {
        // Manual mouse delta (see note above).
        Vector2 md = { 0, 0 };
        const Vector2 mouse = GetMousePosition();
        if (B.hasLastMouse) {
            md = Vector2Subtract(mouse, B.lastMouse);
            if (Vector2Length(md) > 80.0f) md = { 0, 0 };
        }
        B.lastMouse = mouse;
        B.hasLastMouse = true;

        B.yaw   -= md.x * 0.003f;
        B.pitch = Clamp(B.pitch - md.y * 0.003f, -0.4f, 0.6f);
        fwd = Vector3{ sinf(B.yaw), 0, cosf(B.yaw) };
        const Vector3 right = { fwd.z, 0, -fwd.x };

        // Track recent motion (decaying) so an attack reads the latest flick.
        B.aimAccum = Vector2Add(Vector2Scale(B.aimAccum, 0.6f), md);

        Vector3 move = { 0, 0, 0 };
        if (IsKeyDown(KEY_W)) move = Vector3Add(move, fwd);
        if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, fwd);
        if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
        if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);
        if (Vector3Length(move) > 0) {
            const float speed = B.blocking ? 3.0f : 7.0f;
            B.pPos = Vector3Add(B.pPos, Vector3Scale(Vector3Normalize(move), speed * dt));
            B.walkPhase += dt * 10.0f;
        }
        B.pPos.x = Clamp(B.pPos.x, -ARENA, ARENA);
        B.pPos.z = Clamp(B.pPos.z, -ARENA, ARENA);

        if (IsKeyPressed(KEY_SPACE) && B.pPos.y <= 0.01f) B.vY = 6.0f;
        B.vY -= 18.0f * dt;
        B.pPos.y += B.vY * dt;
        if (B.pPos.y < 0) { B.pPos.y = 0; B.vY = 0; }

        B.blocking = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
        B.cooldown -= dt;
        if (B.swing > 0) B.swing -= dt * 4.0f;

        // ---- attack: direction chosen from recent mouse motion ----
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && B.cooldown <= 0 && !B.blocking) {
            B.attackDir = DirFromMotion(B.aimAccum);
            B.cooldown = 0.7f;
            B.swing = 1.0f;

            for (Soldier& s : B.soldiers) {
                if (s.hp <= 0 || s.team != Team::Enemy) continue;
                Vector3 to = Vector3Subtract(s.pos, B.pPos);
                to.y = 0;
                const float d = Vector3Length(to);
                const int wh = gs.playerHero.loadout.get(EquipSlot::Weapon);
                const float reach = c.weapons.valid(wh) && c.weapons[wh].reach > 0.5f
                                        ? c.weapons[wh].reach : 2.6f;
                if (d < reach + 0.6f && d > 0.01f &&
                    Vector3DotProduct(Vector3Normalize(to), fwd) > 0.4f) {
                    s.hp -= HIT_DAMAGE;   // ~120° frontal arc
                }
            }
        }
    } else {
        B.overTimer -= dt;
        if (B.overTimer <= 0) {
            gs.screen = Screen::BattleResult;
            CampaignUpdateDraw(gs, dt);
            return;
        }
    }

    // ---------- soldier AI ----------
    int aliveAllies = 0, aliveEnemies = 0;
    for (const Soldier& s : B.soldiers) {
        if (s.hp <= 0) continue;
        (s.team == Team::Enemy ? aliveEnemies : aliveAllies)++;
    }

    for (int i = 0; i < (int)B.soldiers.size() && !B.over; ++i) {
        Soldier& s = B.soldiers[i];
        if (s.hp <= 0) continue;
        s.cooldown -= dt;
        if (s.swing > 0) s.swing -= dt * 4.0f;

        const Team foe = (s.team == Team::Player) ? Team::Enemy : Team::Player;
        if (s.target < 0 || s.target >= (int)B.soldiers.size() ||
            B.soldiers[s.target].hp <= 0 || B.soldiers[s.target].team == s.team) {
            s.target = FindNearest(s, foe);
        }

        // Enemy soldiers may prefer the player if closer.
        bool targetPlayer = false;
        Vector3 tpos;
        if (s.team == Team::Enemy) {
            const float dp = Vector3DistanceSqr(s.pos, B.pPos);
            if (s.target < 0 || dp < Vector3DistanceSqr(s.pos, B.soldiers[s.target].pos)) {
                targetPlayer = true;
                tpos = B.pPos;
            }
        }
        if (!targetPlayer) {
            if (s.target < 0) continue;
            tpos = B.soldiers[s.target].pos;
        }

        Vector3 to = Vector3Subtract(tpos, s.pos);
        to.y = 0;
        const float dist = Vector3Length(to);
        if (dist > 0.01f) s.yaw = atan2f(to.x, to.z);

        const float reach = c.weapons.valid(TroopLoadout(gs, s.troop).get(EquipSlot::Weapon))
                                ? c.weapons[TroopLoadout(gs, s.troop).get(EquipSlot::Weapon)].reach
                                : 0.0f;
        const float engage = (reach > 0.5f ? reach : 2.6f) * 0.8f;
        if (dist > engage) {
            s.pos = Vector3Add(s.pos, Vector3Scale(Vector3Normalize(to),
                                                   c.troops[s.troop].moveSpeed * dt));
            s.walkPhase += dt * 10.0f;
        } else if (s.cooldown <= 0) {
            s.cooldown = ATTACK_COOLDOWN;
            s.swing = 1.0f;
            const float dmg = HIT_DAMAGE;   // placeholder; per-weapon/troop later
            if (targetPlayer) {
                B.pHp -= B.blocking ? dmg * 0.15f : dmg;
            } else {
                B.soldiers[s.target].hp -= dmg;
            }
        }

        // Separation so soldiers don't stack.
        for (int j = i + 1; j < (int)B.soldiers.size(); ++j) {
            Soldier& o = B.soldiers[j];
            if (o.hp <= 0) continue;
            Vector3 d = Vector3Subtract(s.pos, o.pos);
            d.y = 0;
            const float len = Vector3Length(d);
            if (len < SOLDIER_RADIUS * 2 && len > 0.001f) {
                Vector3 push = Vector3Scale(Vector3Normalize(d), (SOLDIER_RADIUS * 2 - len) * 0.5f);
                s.pos = Vector3Add(s.pos, push);
                o.pos = Vector3Subtract(o.pos, push);
            }
        }
    }

    // ---------- win / lose ----------
    if (!B.over) {
        if (B.pHp <= 0) EndBattle(gs, false);
        else if (aliveEnemies == 0) EndBattle(gs, true);
    }

    // ---------- camera ----------
    Camera3D cam = { 0 };
    const Vector3 look = { sinf(B.yaw) * cosf(B.pitch), sinf(B.pitch), cosf(B.yaw) * cosf(B.pitch) };
    const Vector3 eye = { B.pPos.x, B.pPos.y + 2.0f, B.pPos.z };
    cam.position = Vector3Subtract(eye, Vector3Scale(look, 6.0f));
    cam.position.y = fmaxf(cam.position.y, 0.5f);
    cam.target = Vector3Add(eye, Vector3Scale(look, 4.0f));
    cam.up = { 0, 1, 0 };
    cam.fovy = 60;
    cam.projection = CAMERA_PERSPECTIVE;

    // ================= DRAW =================
    BeginDrawing();
    ClearBackground(Color{ 150, 190, 230, 255 });

    BeginMode3D(cam);
    DrawPlane({ 0, 0, 0 }, { ARENA * 2, ARENA * 2 }, Color{ 88, 120, 68, 255 });
    for (int g = -(int)ARENA; g <= (int)ARENA; g += 10)
        DrawLine3D({ (float)g, 0.01f, -ARENA }, { (float)g, 0.01f, ARENA }, Fade(DARKGREEN, 0.4f));

    for (const Soldier& s : B.soldiers) {
        if (s.hp <= 0) {
            DrawCube({ s.pos.x, 0.15f, s.pos.z }, 1.4f, 0.3f, 0.6f, Fade(DARKGRAY, 0.7f));
            continue;
        }
        Pose pose;
        pose.yaw = s.yaw;
        pose.swing = s.swing;
        pose.walkPhase = s.walkPhase;
        DrawCharacter(c, s.pos, TroopLoadout(gs, s.troop), pose, TeamTint(s.team));
        // health bar
        const float frac = s.hp / s.maxHp;
        DrawCube({ s.pos.x, 2.5f, s.pos.z }, 1.2f * frac, 0.08f, 0.08f,
                 s.team == Team::Enemy ? RED : GREEN);
    }

    // player avatar
    Pose ppose;
    ppose.yaw = B.yaw;
    ppose.swing = B.swing;
    ppose.attackDir = B.attackDir;
    ppose.blocking = B.blocking;
    ppose.walkPhase = B.walkPhase;
    DrawCharacter(c, B.pPos, gs.playerHero.loadout, ppose, Color{ 40, 120, 255, 255 });

    EndMode3D();

    // ---------- HUD ----------
    DrawRectangle(18, GetScreenHeight() - 42, 300, 22, Fade(BLACK, 0.5f));
    DrawRectangle(20, GetScreenHeight() - 40, (int)(296 * fmaxf(B.pHp, 0) / B.pMaxHp), 18, RED);
    DrawText("HP", 24, GetScreenHeight() - 40, 18, RAYWHITE);
    DrawText(TextFormat("Allies: %d   Enemies: %d", aliveAllies, aliveEnemies), 18, 12, 22, RAYWHITE);
    DrawText("LMB attack (flick mouse to aim the swing) | RMB block | WASD move | SPACE jump",
             18, 38, 16, Fade(RAYWHITE, 0.7f));

    const char* dirName[] = { "UP", "DOWN", "LEFT", "RIGHT" };
    DrawText(TextFormat("Next swing: %s", dirName[(int)B.attackDir]), 18, 60, 16, GOLD);

    DrawLine(GetScreenWidth() / 2 - 8, GetScreenHeight() / 2, GetScreenWidth() / 2 + 8, GetScreenHeight() / 2, RAYWHITE);
    DrawLine(GetScreenWidth() / 2, GetScreenHeight() / 2 - 8, GetScreenWidth() / 2, GetScreenHeight() / 2 + 8, RAYWHITE);

    if (B.over) {
        const char* msg = B.won ? "VICTORY!" : "YOU HAVE FALLEN";
        const int w = MeasureText(msg, 60);
        DrawText(msg, (GetScreenWidth() - w) / 2, GetScreenHeight() / 2 - 60, 60, B.won ? GOLD : RED);
    }

    EndDrawing();
}
