#include "game.h"
#include <vector>
#include <cstring>

// Mount & Blade style real-time battle: third-person 3D. You fight alongside
// your troops. WASD move, mouse look, LMB swing, RMB block, SPACE jump.
// Battle ends when one side is wiped out (or you fall).

static const float ARENA = 90.0f;        // half-size of battlefield
static const float SOLDIER_RADIUS = 0.6f;
static const float ATTACK_RANGE = 2.6f;
static const float ATTACK_COOLDOWN = 1.1f;

struct Soldier {
    Vector3 pos;
    int type;           // TROOP_TYPES index
    float hp;
    bool enemy;
    float cooldown = 0;
    int targetIdx = -1;
    float swing = 0;    // >0 while swinging (visual)
};

struct BattleState {
    std::vector<Soldier> soldiers;
    // player
    Vector3 pPos;
    float pHp, pMaxHp;
    float yaw = 0, pitch = 0;
    float pCooldown = 0;
    float pSwing = 0;       // swing animation timer
    bool blocking = false;
    float vY = 0;
    bool over = false;
    float overTimer = 0;
    bool won = false;
    int startAllies[TROOP_COUNT];
    // manual mouse tracking (GetMouseDelta is unreliable in WSL)
    Vector2 lastMousePos = {0, 0};
    bool hasLastMouse = false;
};

static BattleState B;

static void SpawnLine(GameState& gs, bool enemy, const int troops[TROOP_COUNT], float zBase) {
    int n = 0;
    for (int t = TROOP_COUNT - 1; t >= 0; t--) {
        for (int i = 0; i < troops[t]; i++) {
            Soldier s;
            s.type = t;
            s.enemy = enemy;
            s.hp = (float)TROOP_TYPES[t].hp;
            float x = -20.0f + (n % 10) * 4.0f;
            float z = zBase + (n / 10) * 4.0f * (enemy ? 1.0f : -1.0f);
            s.pos = { x, 0, z };
            B.soldiers.push_back(s);
            n++;
        }
    }
}

void BattleInit(GameState& gs) {
    B = BattleState{};
    Party& e = gs.enemies[gs.battleEnemyIndex];
    SpawnLine(gs, false, gs.player.troops, -30.0f);
    SpawnLine(gs, true, e.troops, 30.0f);
    memcpy(B.startAllies, gs.player.troops, sizeof(B.startAllies));

    B.pPos = { 0, 0, -38 };
    B.pMaxHp = 150;
    B.pHp = B.pMaxHp;
    B.yaw = 0;

    memset(gs.playerLosses, 0, sizeof(gs.playerLosses));
    DisableCursor();
    B.hasLastMouse = false;
}

static int FindNearest(const Soldier& me, bool wantEnemy) {
    int best = -1;
    float bestD = 1e9f;
    for (int i = 0; i < (int)B.soldiers.size(); i++) {
        const Soldier& o = B.soldiers[i];
        if (o.hp <= 0 || o.enemy != wantEnemy) continue;
        float d = Vector3DistanceSqr(me.pos, o.pos);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

static void EndBattle(GameState& gs, bool won) {
    B.over = true;
    B.won = won;
    B.overTimer = 2.5f;

    // count surviving allies per type -> losses
    int survivors[TROOP_COUNT] = { 0, 0, 0 };
    for (auto& s : B.soldiers)
        if (!s.enemy && s.hp > 0) survivors[s.type]++;
    for (int t = 0; t < TROOP_COUNT; t++)
        gs.playerLosses[t] = B.startAllies[t] - survivors[t];
    gs.battleWon = won;
    EnableCursor();
}

void BattleUpdateDraw(GameState& gs, float dt) {
    if (dt > 0.05f) dt = 0.05f;

    // ---------- player input ----------
    if (!B.over) {
        // Manual mouse tracking — GetMouseDelta + DisableCursor are unreliable
        // under WSL / X11 forwarding.  Compute the delta from GetMousePosition
        // and skip huge jumps (cursor re-entry, WSL glitches).
        Vector2 md = { 0, 0 };
        Vector2 mousePos = GetMousePosition();
        if (B.hasLastMouse) {
            md.x = mousePos.x - B.lastMousePos.x;
            md.y = mousePos.y - B.lastMousePos.y;
            if (Vector2Length(md) > 80.0f) md = { 0, 0 };
        }
        B.lastMousePos = mousePos;
        B.hasLastMouse = true;

        B.yaw   -= md.x * 0.003f;
        B.pitch -= md.y * 0.003f;
        B.pitch = Clamp(B.pitch, -0.4f, 0.6f);

        Vector3 fwd = { sinf(B.yaw), 0, cosf(B.yaw) };
        Vector3 right = { fwd.z, 0, -fwd.x };
        Vector3 move = { 0, 0, 0 };
        if (IsKeyDown(KEY_W)) move = Vector3Add(move, fwd);
        if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, fwd);
        if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
        if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);
        if (Vector3Length(move) > 0) {
            float speed = B.blocking ? 3.0f : 7.0f;
            B.pPos = Vector3Add(B.pPos, Vector3Scale(Vector3Normalize(move), speed * dt));
        }
        B.pPos.x = Clamp(B.pPos.x, -ARENA, ARENA);
        B.pPos.z = Clamp(B.pPos.z, -ARENA, ARENA);

        // jump / gravity
        if (IsKeyPressed(KEY_SPACE) && B.pPos.y <= 0.01f) B.vY = 6.0f;
        B.vY -= 18.0f * dt;
        B.pPos.y += B.vY * dt;
        if (B.pPos.y < 0) { B.pPos.y = 0; B.vY = 0; }

        B.blocking = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
        B.pCooldown -= dt;
        B.pSwing -= dt;

        // attack: arc in front of player
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && B.pCooldown <= 0 && !B.blocking) {
            B.pCooldown = 0.7f;
            B.pSwing = 0.25f;
            for (auto& s : B.soldiers) {
                if (s.hp <= 0 || !s.enemy) continue;
                Vector3 to = Vector3Subtract(s.pos, B.pPos);
                to.y = 0;
                float d = Vector3Length(to);
                if (d < ATTACK_RANGE + 0.6f && d > 0.01f) {
                    float dot = Vector3DotProduct(Vector3Normalize(to), fwd);
                    if (dot > 0.4f) s.hp -= 35.0f;   // ~120 degree arc
                }
            }
        }
    } else {
        B.overTimer -= dt;
        if (B.overTimer <= 0) {
            gs.screen = Screen::BattleResult;
            CampaignUpdateDraw(gs, dt);   // hand back immediately
            return;
        }
    }

    // ---------- soldier AI ----------
    int aliveAllies = 0, aliveEnemies = 0;
    for (auto& s : B.soldiers) {
        if (s.hp <= 0) continue;
        (s.enemy ? aliveEnemies : aliveAllies)++;
    }

    for (int i = 0; i < (int)B.soldiers.size() && !B.over; i++) {
        Soldier& s = B.soldiers[i];
        if (s.hp <= 0) continue;
        s.cooldown -= dt;
        s.swing -= dt;

        // pick / validate target
        if (s.targetIdx < 0 || s.targetIdx >= (int)B.soldiers.size() ||
            B.soldiers[s.targetIdx].hp <= 0 || B.soldiers[s.targetIdx].enemy == s.enemy) {
            s.targetIdx = FindNearest(s, !s.enemy);
        }

        // enemies may also target the player
        bool targetPlayer = false;
        Vector3 tpos;
        if (s.enemy) {
            float dp = Vector3DistanceSqr(s.pos, B.pPos);
            if (s.targetIdx < 0 || dp < Vector3DistanceSqr(s.pos, B.soldiers[s.targetIdx].pos)) {
                targetPlayer = true;
                tpos = B.pPos;
            }
        }
        if (!targetPlayer) {
            if (s.targetIdx < 0) continue;   // nothing left to fight
            tpos = B.soldiers[s.targetIdx].pos;
        }

        Vector3 to = Vector3Subtract(tpos, s.pos);
        to.y = 0;
        float dist = Vector3Length(to);
        if (dist > ATTACK_RANGE * 0.8f) {
            Vector3 step = Vector3Scale(Vector3Normalize(to), TROOP_TYPES[s.type].speed * dt);
            s.pos = Vector3Add(s.pos, step);
        } else if (s.cooldown <= 0) {
            s.cooldown = ATTACK_COOLDOWN;
            s.swing = 0.25f;
            float dmg = TROOP_TYPES[s.type].damage;
            if (targetPlayer) {
                if (B.blocking) dmg *= 0.15f;
                B.pHp -= dmg;
            } else {
                B.soldiers[s.targetIdx].hp -= dmg;
            }
        }

        // simple separation so soldiers don't stack
        for (int j = i + 1; j < (int)B.soldiers.size(); j++) {
            Soldier& o = B.soldiers[j];
            if (o.hp <= 0) continue;
            Vector3 d = Vector3Subtract(s.pos, o.pos);
            d.y = 0;
            float len = Vector3Length(d);
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
        // if all allies fall, the player can still fight on solo
    }

    // ---------- camera ----------
    Camera3D cam = { 0 };
    Vector3 fwd = {
        sinf(B.yaw) * cosf(B.pitch),
        sinf(B.pitch),
        cosf(B.yaw) * cosf(B.pitch),
    };
    Vector3 eye = { B.pPos.x, B.pPos.y + 2.0f, B.pPos.z };
    cam.position = Vector3Subtract(eye, Vector3Scale(fwd, 6.0f));
    cam.position.y = fmaxf(cam.position.y, 0.5f);
    cam.target = Vector3Add(eye, Vector3Scale(fwd, 4.0f));
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

    for (auto& s : B.soldiers) {
        if (s.hp <= 0) {
            DrawCube({ s.pos.x, 0.15f, s.pos.z }, 1.4f, 0.3f, 0.6f, Fade(DARKGRAY, 0.7f));
            continue;
        }
        Color body = s.enemy ? RED : TROOP_TYPES[s.type].color;
        Vector3 c = { s.pos.x, 0.9f, s.pos.z };
        DrawCapsule({ s.pos.x, 0.4f, s.pos.z }, { s.pos.x, 1.4f, s.pos.z }, 0.45f, 8, 8, body);
        DrawSphere({ s.pos.x, 1.9f, s.pos.z }, 0.3f, s.enemy ? MAROON : BEIGE);
        if (s.swing > 0)   // weapon flash while attacking
            DrawSphere({ s.pos.x, 1.3f, s.pos.z }, 0.7f, Fade(YELLOW, 0.5f));
        // hp bar
        float frac = s.hp / (float)TROOP_TYPES[s.type].hp;
        DrawCube({ s.pos.x, 2.4f, s.pos.z }, 1.2f * frac, 0.08f, 0.08f, s.enemy ? RED : GREEN);
        (void)c;
    }

    // player body
    DrawCapsule({ B.pPos.x, B.pPos.y + 0.4f, B.pPos.z }, { B.pPos.x, B.pPos.y + 1.4f, B.pPos.z },
                0.45f, 8, 8, BLUE);
    DrawSphere({ B.pPos.x, B.pPos.y + 1.9f, B.pPos.z }, 0.3f, BEIGE);
    // sword
    Vector3 swordDir = { sinf(B.yaw), 0, cosf(B.yaw) };
    Vector3 hilt = { B.pPos.x + swordDir.x * 0.5f, B.pPos.y + 1.2f, B.pPos.z + swordDir.z * 0.5f };
    Vector3 tip = Vector3Add(hilt, Vector3Scale(swordDir, B.pSwing > 0 ? 2.2f : 1.4f));
    tip.y += B.pSwing > 0 ? 0.4f : 0.9f;
    DrawCylinderEx(hilt, tip, 0.06f, 0.03f, 6, B.blocking ? DARKGRAY : LIGHTGRAY);

    EndMode3D();

    // ---------- HUD ----------
    DrawRectangle(18, GetScreenHeight() - 42, 300, 22, Fade(BLACK, 0.5f));
    DrawRectangle(20, GetScreenHeight() - 40, (int)(296 * fmaxf(B.pHp, 0) / B.pMaxHp), 18, RED);
    DrawText("HP", 24, GetScreenHeight() - 40, 18, RAYWHITE);
    DrawText(TextFormat("Allies: %d   Enemies: %d", aliveAllies, aliveEnemies), 18, 12, 22, RAYWHITE);
    DrawText("LMB attack | RMB block | WASD move | SPACE jump", 18, 38, 16, Fade(RAYWHITE, 0.7f));

    // crosshair
    DrawLine(GetScreenWidth() / 2 - 8, GetScreenHeight() / 2, GetScreenWidth() / 2 + 8, GetScreenHeight() / 2, RAYWHITE);
    DrawLine(GetScreenWidth() / 2, GetScreenHeight() / 2 - 8, GetScreenWidth() / 2, GetScreenHeight() / 2 + 8, RAYWHITE);

    if (B.over) {
        const char* msg = B.won ? "VICTORY!" : "YOU HAVE FALLEN";
        int w = MeasureText(msg, 60);
        DrawText(msg, (GetScreenWidth() - w) / 2, GetScreenHeight() / 2 - 60, 60, B.won ? GOLD : RED);
    }

    EndDrawing();
}
