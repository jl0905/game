#include "game.h"
#include <cmath>

// ---------------------------------------------------------------------------
// Campaign map: top-down overworld. Your party moves toward the mouse (hold
// LMB) or with WASD. Other factions' parties roam by their own behaviour and
// may chase you; touching one starts a battle. Towns let you recruit from your
// faction's roster.
// ---------------------------------------------------------------------------

namespace {

constexpr float MAP_SIZE   = 2000.0f;
constexpr float PARTY_SPEED = 160.0f;
constexpr float TOWN_RANGE  = 60.0f;

float Frand(float a, float b) {
    return a + (b - a) * (GetRandomValue(0, 10000) / 10000.0f);
}

// Build a party for a faction, filling its roster with a few troops each.
Party MakeParty(const Content& c, int faction, Vector2 pos) {
    Party p;
    p.faction = faction;
    p.pos = pos;
    p.wanderTarget = pos;
    p.troopCounts.assign(c.troops.size(), 0);
    for (int troop : c.factions[faction].roster)
        p.troopCounts[troop] = GetRandomValue(1, 4);
    return p;
}

Vector2 RandomEdgePos() {
    return { Frand(150, MAP_SIZE - 150), Frand(150, MAP_SIZE - 150) };
}

// Non-player factions that can spawn as roaming parties.
std::vector<int> RoamingFactions(const Content& c) {
    std::vector<int> out;
    for (int i = 0; i < c.factions.size(); ++i)
        if (i != c.playerFaction) out.push_back(i);
    return out;
}

}  // namespace

void CampaignInit(GameState& gs) {
    const Content& c = gs.content;

    // Player party + hero avatar.
    gs.player = Party{};
    gs.player.isPlayer = true;
    gs.player.faction = c.playerFaction;
    gs.player.pos = { MAP_SIZE / 2, MAP_SIZE / 2 };
    gs.player.troopCounts.assign(c.troops.size(), 0);
    if (const int r = c.troops.find("recruit");  r >= 0) gs.player.troopCounts[r] = 5;
    if (const int i = c.troops.find("infantry"); i >= 0) gs.player.troopCounts[i] = 2;

    // Give the hero a starting loadout (models are driven by these handles).
    gs.playerHero = Character{};
    Loadout& hl = gs.playerHero.loadout;
    hl.set(EquipSlot::Head,   c.armor.find("helmet"));
    hl.set(EquipSlot::Body,   c.armor.find("plate"));
    hl.set(EquipSlot::Hands,  c.armor.find("gloves"));
    hl.set(EquipSlot::Feet,   c.armor.find("boots"));
    hl.set(EquipSlot::Weapon, c.weapons.find("sword"));

    gs.towns = {
        { { 400, 400 },   "Sargoth" },
        { { 1600, 500 },  "Praven" },
        { { 500, 1550 },  "Tulga" },
        { { 1500, 1500 }, "Jelkala" },
    };

    gs.parties.clear();
    gs.playerLosses.assign(c.troops.size(), 0);
    const std::vector<int> roamers = RoamingFactions(c);
    for (int i = 0; i < 5; ++i)
        gs.parties.push_back(MakeParty(c, roamers[i % roamers.size()], RandomEdgePos()));
}

static void ApplyBattleResult(GameState& gs) {
    for (int t = 0; t < (int)gs.player.troopCounts.size(); ++t) {
        gs.player.troopCounts[t] -= gs.playerLosses[t];
        if (gs.player.troopCounts[t] < 0) gs.player.troopCounts[t] = 0;
    }
    if (gs.battleWon) {
        const int loot = 50 + GetRandomValue(0, 100);
        gs.gold += loot;
        gs.resultText = TextFormat("VICTORY!  Loot: %d gold", loot);
        if (gs.battlePartyIndex >= 0 && gs.battlePartyIndex < (int)gs.parties.size())
            gs.parties[gs.battlePartyIndex].alive = false;
    } else {
        gs.resultText = "DEFEAT...  You escape with the survivors.";
        gs.player.pos.x = Clamp(gs.player.pos.x + Frand(-300, 300), 100, MAP_SIZE - 100);
        gs.player.pos.y = Clamp(gs.player.pos.y + Frand(-300, 300), 100, MAP_SIZE - 100);
    }
    gs.battlePartyIndex = -1;
}

// Decide a roaming party's steering target for this frame based on its faction
// behaviour. Returns the point it should move toward.
static Vector2 PartyTarget(GameState& gs, Party& e, float dt) {
    const PartyBehavior behavior = gs.content.factions[e.faction].behavior;
    const float distToPlayer = Vector2Distance(e.pos, gs.player.pos);
    const int playerStrength = gs.player.totalTroops();

    e.thinkTimer -= dt;
    const bool couldWin = e.totalTroops() >= playerStrength / 2;

    switch (behavior) {
        case PartyBehavior::Aggressive:
            if (distToPlayer < 500) return gs.player.pos;               // hunts eagerly
            break;
        case PartyBehavior::Patrol:
            if (distToPlayer < 300 && couldWin) return gs.player.pos;   // opportunistic
            break;
        case PartyBehavior::Passive:
            if (distToPlayer < 220) {                                   // flees
                Vector2 away = Vector2Subtract(e.pos, gs.player.pos);
                if (Vector2Length(away) > 1)
                    return Vector2Add(e.pos, Vector2Scale(Vector2Normalize(away), 200));
            }
            break;
    }
    if (e.thinkTimer <= 0) {
        e.wanderTarget = { Frand(100, MAP_SIZE - 100), Frand(100, MAP_SIZE - 100) };
        e.thinkTimer = Frand(3, 8);
    }
    return e.wanderTarget;
}

void CampaignUpdateDraw(GameState& gs, float dt) {
    const Content& c = gs.content;

    if (gs.screen == Screen::BattleResult) {
        ApplyBattleResult(gs);
        gs.screen = Screen::Campaign;
    }

    // ---- player movement ----
    Camera2D cam = { 0 };
    cam.target = gs.player.pos;
    cam.offset = { GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f };
    cam.zoom = 1.0f;

    Vector2 move = { 0, 0 };
    if (IsKeyDown(KEY_W)) move.y -= 1;
    if (IsKeyDown(KEY_S)) move.y += 1;
    if (IsKeyDown(KEY_A)) move.x -= 1;
    if (IsKeyDown(KEY_D)) move.x += 1;
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Vector2 world = GetScreenToWorld2D(GetMousePosition(), cam);
        Vector2 dir = Vector2Subtract(world, gs.player.pos);
        if (Vector2Length(dir) > 8) move = Vector2Normalize(dir);
    }
    if (Vector2Length(move) > 0) {
        move = Vector2Normalize(move);
        gs.player.pos = Vector2Add(gs.player.pos, Vector2Scale(move, PARTY_SPEED * dt));
    }
    gs.player.pos.x = Clamp(gs.player.pos.x, 0, MAP_SIZE);
    gs.player.pos.y = Clamp(gs.player.pos.y, 0, MAP_SIZE);

    // ---- towns: recruit from the player faction roster ----
    gs.nearTown = -1;
    for (int i = 0; i < (int)gs.towns.size(); ++i)
        if (Vector2Distance(gs.player.pos, gs.towns[i].pos) < TOWN_RANGE) gs.nearTown = i;

    const std::vector<int>& roster = c.factions[c.playerFaction].roster;
    if (gs.nearTown >= 0) {
        for (int slot = 0; slot < (int)roster.size(); ++slot) {
            const TroopDef& td = c.troops[roster[slot]];
            if (IsKeyPressed(KEY_ONE + slot) && gs.gold >= td.cost) {
                gs.gold -= td.cost;
                gs.player.troopCounts[roster[slot]]++;
            }
        }
    }

    // ---- roaming party AI ----
    for (auto& e : gs.parties) {
        if (!e.alive) continue;
        Vector2 target = PartyTarget(gs, e, dt);
        Vector2 dir = Vector2Subtract(target, e.pos);
        if (Vector2Length(dir) > 5)
            e.pos = Vector2Add(e.pos, Vector2Scale(Vector2Normalize(dir), PARTY_SPEED * 0.7f * dt));

        if (Vector2Distance(e.pos, gs.player.pos) < 24 && gs.player.totalTroops() > 0) {
            gs.battlePartyIndex = (int)(&e - &gs.parties[0]);
            gs.screen = Screen::Battle;
            BattleInit(gs);
            return;
        }
    }

    // ---- respawn parties over time ----
    gs.spawnTimer += dt;
    int aliveCount = 0;
    for (auto& e : gs.parties) if (e.alive) aliveCount++;
    if (gs.spawnTimer > 20 && aliveCount < 8) {
        gs.spawnTimer = 0;
        const std::vector<int> roamers = RoamingFactions(c);
        gs.parties.push_back(MakeParty(c, roamers[GetRandomValue(0, (int)roamers.size() - 1)], RandomEdgePos()));
    }

    // ================= DRAW =================
    BeginDrawing();
    ClearBackground(Color{ 60, 92, 48, 255 });

    BeginMode2D(cam);
    DrawRectangleLinesEx(Rectangle{ 0, 0, MAP_SIZE, MAP_SIZE }, 6, DARKBROWN);
    for (int x = 0; x <= (int)MAP_SIZE; x += 200)
        DrawLine(x, 0, x, (int)MAP_SIZE, Color{ 70, 102, 58, 255 });
    for (int y = 0; y <= (int)MAP_SIZE; y += 200)
        DrawLine(0, y, (int)MAP_SIZE, y, Color{ 70, 102, 58, 255 });

    for (const Town& t : gs.towns) {
        DrawRectangle((int)t.pos.x - 20, (int)t.pos.y - 20, 40, 40, BROWN);
        DrawTriangle({ t.pos.x - 24, t.pos.y - 20 }, { t.pos.x + 24, t.pos.y - 20 },
                     { t.pos.x, t.pos.y - 44 }, MAROON);
        DrawText(t.name.c_str(), (int)t.pos.x - 30, (int)t.pos.y + 26, 16, RAYWHITE);
        DrawCircleLines((int)t.pos.x, (int)t.pos.y, TOWN_RANGE, Fade(RAYWHITE, 0.25f));
    }

    for (const Party& e : gs.parties) {
        if (!e.alive) continue;
        const FactionDef& f = c.factions[e.faction];
        DrawCircleV(e.pos, 12, f.color);
        DrawText(TextFormat("%s %d", f.name.c_str(), e.totalTroops()),
                 (int)e.pos.x - 20, (int)e.pos.y - 32, 14, f.color);
    }

    DrawCircleV(gs.player.pos, 12, c.factions[c.playerFaction].color);
    DrawText("You", (int)gs.player.pos.x - 12, (int)gs.player.pos.y - 34, 16, RAYWHITE);
    EndMode2D();

    // ---- HUD ----
    DrawRectangle(0, 0, GetScreenWidth(), 34, Fade(BLACK, 0.6f));
    DrawText(TextFormat("Gold: %d   Party: %d", gs.gold, gs.player.totalTroops()), 10, 8, 20, RAYWHITE);

    if (gs.nearTown >= 0) {
        const int y = GetScreenHeight() - 40 - (int)roster.size() * 24;
        DrawRectangle(0, y - 8, GetScreenWidth(), GetScreenHeight() - y + 8, Fade(BLACK, 0.7f));
        DrawText(TextFormat("Welcome to %s! Recruit troops:", gs.towns[gs.nearTown].name.c_str()),
                 10, y, 20, GOLD);
        for (int slot = 0; slot < (int)roster.size(); ++slot) {
            const TroopDef& td = c.troops[roster[slot]];
            DrawText(TextFormat("[%d] %s - %d gold  (have %d)", slot + 1, td.name.c_str(),
                                td.cost, gs.player.troopCounts[roster[slot]]),
                     10, y + 26 + slot * 24, 20, RAYWHITE);
        }
    } else if (!gs.resultText.empty()) {
        DrawText(gs.resultText.c_str(), 10, 42, 20, GOLD);
    }
    DrawText("Move: WASD or hold LMB. Coloured parties roam by faction. Enter towns to recruit.",
             10, GetScreenHeight() - 22, 16, Fade(RAYWHITE, 0.7f));

    if (gs.player.totalTroops() == 0) {
        DrawText("Your warband is destroyed... press R to restart.", 10, 70, 20, RED);
        if (IsKeyPressed(KEY_R)) { Content saved = std::move(gs.content); gs = GameState{};
                                   gs.content = std::move(saved); CampaignInit(gs); }
    }

    EndDrawing();
}
