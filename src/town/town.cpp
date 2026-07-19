#include "town.h"
#include "../battle/character.h"   // the one humanoid renderer (battle owns it)
#include "../ui.h"
#include "raymath.h"
#include <cmath>
#include <vector>

namespace {

constexpr float TOWN_EDGE    = 55.0f;  // walkable half-size
constexpr float WALK_SPEED   = 6.0f;
constexpr float NPC_SPEED    = 2.2f;
constexpr float TAVERN_RANGE = 9.0f;   // close enough to recruit

// Deterministic per-town rng (same pattern as battle terrain).
struct Rng {
    unsigned int s;
    unsigned int next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float unit() { return (next() & 0xFFFFFF) / (float)0xFFFFFF; }
    float range(float a, float b) { return a + (b - a) * unit(); }
};

struct Building {
    Vector3 pos;      // centre on the ground
    Vector3 size;     // w, h, d
    Color   wall;
    Color   roof;
    bool    tavern = false;
    bool    flatTop = false;   // curtain walls / towers: no pitched roof
};

struct Npc {
    Vector3 pos;
    float   yaw = 0;
    float   walkPhase = 0;
    Vector2 target{};
    float   think = 0;
    Loadout loadout;
};

struct TownScene {
    std::vector<Building> buildings;
    std::vector<Npc>      npcs;
    int     tavern = -1;

    Vector3 pPos{};
    float   yaw = 0, pitch = 0;
    float   walkPhase = 0;

    Vector2 lastMouse{};
    bool    hasLastMouse = false;
};

TownScene T;

// Keep a point out of every building footprint (simple AABB push-out).
Vector3 CollideBuildings(Vector3 p, float radius) {
    for (const Building& b : T.buildings) {
        const float hx = b.size.x * 0.5f + radius;
        const float hz = b.size.z * 0.5f + radius;
        const float dx = p.x - b.pos.x;
        const float dz = p.z - b.pos.z;
        if (fabsf(dx) >= hx || fabsf(dz) >= hz) continue;
        // push out along the shallower axis
        if (hx - fabsf(dx) < hz - fabsf(dz))
            p.x = b.pos.x + (dx < 0 ? -hx : hx);
        else
            p.z = b.pos.z + (dz < 0 ? -hz : hz);
    }
    p.x = Clamp(p.x, -TOWN_EDGE, TOWN_EDGE);
    p.z = Clamp(p.z, -TOWN_EDGE, TOWN_EDGE);
    return p;
}

}  // namespace

void TownInit(const GameState& gs) {
    T = TownScene{};
    if (gs.currentSettlement < 0 || gs.currentSettlement >= (int)gs.towns.size()) return;
    const Town& town = gs.towns[gs.currentSettlement];
    const Content& c = gs.content;

    Rng rng{ (unsigned)(town.pos.x * 73856093) ^ (unsigned)(town.pos.y * 19349663) ^ 7u };

    // Castles are built, not grown: curtain walls with corner towers around a
    // courtyard, and a central keep where the lord holds court (recruiting).
    if (town.type == SettlementType::Castle) {
        const Color stone = Color{ 158, 156, 160, 255 };
        const Color dark  = Color{ 120, 118, 124, 255 };
        const float C = 26.0f;   // courtyard half-size
        auto wall = [&](Vector3 pos, Vector3 size) {
            Building b;
            b.pos = pos; b.size = size;
            b.wall = stone; b.roof = dark; b.flatTop = true;
            T.buildings.push_back(b);
        };
        // north, east, west full walls; south wall split by the gate
        wall({ 0, 0, -C }, { C * 2 + 4, 7, 3 });
        wall({ -C, 0, 0 }, { 3, 7, C * 2 + 4 });
        wall({  C, 0, 0 }, { 3, 7, C * 2 + 4 });
        wall({ -(C * 0.5f + 4.5f), 0, C }, { C - 9, 7, 3 });
        wall({  (C * 0.5f + 4.5f), 0, C }, { C - 9, 7, 3 });
        for (const float tx : { -C, C })              // corner towers
            for (const float tz : { -C, C })
                wall({ tx, 0, tz }, { 7, 11, 7 });

        Building keep;                                 // the keep, centre-north
        keep.pos = { 0, 0, -C * 0.45f };
        keep.size = { 15, 13, 13 };
        keep.wall = stone; keep.roof = GOLD;
        keep.tavern = true;                            // recruit in the hall
        T.buildings.push_back(keep);
        T.tavern = (int)T.buildings.size() - 1;

        // The garrison drills in the yard: armoured guards, not villagers.
        const int a_mail   = c.armor.find("mail");
        const int a_helm   = c.armor.find("helmet");
        const int a_bootsG = c.armor.find("boots");
        for (int i = 0; i < 5; ++i) {
            Npc n;
            n.pos = { rng.range(-14, 14), 0, rng.range(2, 18) };
            n.target = { rng.range(-14, 14), rng.range(2, 18) };
            n.loadout.set(EquipSlot::Body, a_mail);
            n.loadout.set(EquipSlot::Head, a_helm);
            n.loadout.set(EquipSlot::Feet, a_bootsG);
            T.npcs.push_back(n);
        }

        T.pPos = { 0, 0, C + 8 };   // start outside the gate...
        T.yaw  = PI;                // ...facing in through it
        if (IsWindowReady()) DisableCursor();
        T.hasLastMouse = false;
        return;
    }

    // Buildings ring a central plaza; count scales with settlement type
    // (identity, not balance).
    int count = 8;
    if (town.type == SettlementType::Village) count = 5;
    for (int i = 0; i < count; ++i) {
        Building b;
        const float ang = (2.0f * PI * i) / count + rng.range(-0.15f, 0.15f);
        const float rad = rng.range(20.0f, 34.0f);
        b.pos  = { cosf(ang) * rad, 0, sinf(ang) * rad };
        b.size = { rng.range(6, 11), rng.range(4, 7), rng.range(6, 11) };
        const unsigned char wallTone = (unsigned char)rng.range(140, 200);
        b.wall = town.type == SettlementType::Castle
                     ? Color{ wallTone, wallTone, wallTone, 255 }             // stone
                     : Color{ wallTone, (unsigned char)(wallTone * 0.8f),
                              (unsigned char)(wallTone * 0.55f), 255 };       // timber
        b.roof = Color{ (unsigned char)rng.range(120, 170), 60, 50, 255 };
        T.buildings.push_back(b);
    }
    T.tavern = 0;                      // first building is the tavern
    T.buildings[0].tavern = true;
    T.buildings[0].roof = GOLD;        // gilt roof so it reads from the street

    // Villagers: unarmed folk in simple clothes drifting between spots.
    const int a_tunic = c.armor.find("tunic");
    const int a_boots = c.armor.find("boots");
    const int a_cap   = c.armor.find("cap");
    int folk = 10;
    if (town.type == SettlementType::Village) folk = 6;
    if (town.type == SettlementType::Castle)  folk = 6;
    for (int i = 0; i < folk; ++i) {
        Npc n;
        n.pos = { rng.range(-16, 16), 0, rng.range(-16, 16) };
        n.target = { rng.range(-20, 20), rng.range(-20, 20) };
        n.loadout.set(EquipSlot::Body, a_tunic);
        n.loadout.set(EquipSlot::Feet, a_boots);
        if (rng.unit() > 0.5f) n.loadout.set(EquipSlot::Head, a_cap);
        T.npcs.push_back(n);
    }

    T.pPos = { 0, 0, -14 };            // start at the plaza's south edge
    if (IsWindowReady()) DisableCursor();
    T.hasLastMouse = false;
}

bool TownUpdate(GameState& gs, float dt, const BattleInput& in, const CampaignInput& cin) {
    if (dt > 0.05f) dt = 0.05f;
    const Content& c = gs.content;

    // ---- leave ----
    if (cin.leaveSettlement) {
        gs.currentSettlement = -1;
        gs.screen = Screen::Campaign;
        if (IsWindowReady()) EnableCursor();
        return false;
    }

    // ---- hero movement (battle-style third person) ----
    T.yaw   -= in.lookDelta.x * 0.003f;
    T.pitch  = Clamp(T.pitch - in.lookDelta.y * 0.003f, -0.4f, 0.6f);
    const Vector3 fwd   = { sinf(T.yaw), 0, cosf(T.yaw) };
    const Vector3 right = { -fwd.z, 0, fwd.x };
    Vector3 move = Vector3Add(Vector3Scale(fwd, in.moveForward),
                              Vector3Scale(right, in.moveRight));
    if (Vector3Length(move) > 0) {
        T.pPos = Vector3Add(T.pPos, Vector3Scale(Vector3Normalize(move), WALK_SPEED * dt));
        T.walkPhase += dt * 10.0f;
    }
    T.pPos = CollideBuildings(T.pPos, 0.5f);

    // ---- tavern recruiting ----
    if (TownAtTavern() && cin.recruitSlot >= 0) {
        const std::vector<int>& roster = c.factions[c.playerFaction].roster;
        if (cin.recruitSlot < (int)roster.size()) {
            const TroopDef& td = c.troops[roster[cin.recruitSlot]];
            if (gs.gold >= td.cost) {
                gs.gold -= td.cost;
                gs.player.troopCounts[roster[cin.recruitSlot]]++;
            }
        }
    }

    // ---- villagers drift about ----
    for (Npc& n : T.npcs) {
        n.think -= dt;
        const Vector2 to = { n.target.x - n.pos.x, n.target.y - n.pos.z };
        const float d = sqrtf(to.x * to.x + to.y * to.y);
        if (d < 1.0f || n.think <= 0) {
            n.target = { (float)GetRandomValue(-22, 22), (float)GetRandomValue(-22, 22) };
            n.think = (float)GetRandomValue(4, 10);
        } else {
            n.yaw = atan2f(to.x, to.y);
            n.pos.x += (to.x / d) * NPC_SPEED * dt;
            n.pos.z += (to.y / d) * NPC_SPEED * dt;
            n.walkPhase += dt * 6.0f;
        }
        n.pos = CollideBuildings(n.pos, 0.4f);
    }
    return true;
}

TownView GetTownView() {
    TownView v;
    v.heroPos = T.pPos;
    v.heroYaw = T.yaw;
    if (T.tavern >= 0 && T.tavern < (int)T.buildings.size())
        v.tavernPos = T.buildings[T.tavern].pos;
    v.npcs = (int)T.npcs.size();
    v.atTavern = TownAtTavern();
    return v;
}

bool TownAtTavern() {
    if (T.tavern < 0 || T.tavern >= (int)T.buildings.size()) return false;
    const Building& b = T.buildings[T.tavern];
    const float dx = T.pPos.x - b.pos.x, dz = T.pPos.z - b.pos.z;
    return sqrtf(dx * dx + dz * dz) < TAVERN_RANGE + fmaxf(b.size.x, b.size.z) * 0.5f;
}

void TownDraw(const GameState& gs) {
    const Content& c = gs.content;
    const Town& town = gs.towns[gs.currentSettlement >= 0 ? gs.currentSettlement : 0];

    Camera3D cam = { 0 };
    const Vector3 look = { sinf(T.yaw) * cosf(T.pitch), sinf(T.pitch), cosf(T.yaw) * cosf(T.pitch) };
    const Vector3 eye = { T.pPos.x, T.pPos.y + 2.0f, T.pPos.z };
    cam.position = Vector3Subtract(eye, Vector3Scale(look, 6.0f));
    cam.position.y = fmaxf(cam.position.y, 0.6f);
    cam.target = Vector3Add(eye, Vector3Scale(look, 4.0f));
    cam.up = { 0, 1, 0 };
    cam.fovy = 60;
    cam.projection = CAMERA_PERSPECTIVE;

    BeginDrawing();
    ClearBackground(Color{ 168, 200 - 10, 226, 255 });

    BeginMode3D(cam);
    DrawPlane({ 0, 0, 0 }, { TOWN_EDGE * 2, TOWN_EDGE * 2 }, Color{ 96, 128, 72, 255 });
    DrawCylinder({ 0, 0.01f, 0 }, 16.0f, 16.0f, 0.02f, 24, Color{ 150, 134, 105, 255 }); // plaza
    DrawCylinder({ 0, 0.02f, 0 }, 1.2f, 1.4f, 0.9f, 12, Color{ 120, 110, 100, 255 });    // well

    for (const Building& b : T.buildings) {
        DrawCube({ b.pos.x, b.size.y * 0.5f, b.pos.z }, b.size.x, b.size.y, b.size.z, b.wall);
        DrawCubeWires({ b.pos.x, b.size.y * 0.5f, b.pos.z }, b.size.x, b.size.y, b.size.z,
                      Fade(BLACK, 0.35f));
        if (b.flatTop) {   // crenellated top instead of a roof
            for (float cx = -b.size.x / 2; cx < b.size.x / 2; cx += 2.4f)
                DrawCube({ b.pos.x + cx + 0.6f, b.size.y + 0.4f, b.pos.z },
                         1.1f, 0.8f, fminf(b.size.z, 1.6f), b.roof);
        } else {
            // pitched roof: a squashed 4-sided cone
            DrawCylinderEx({ b.pos.x, b.size.y, b.pos.z },
                           { b.pos.x, b.size.y + 2.4f, b.pos.z },
                           fmaxf(b.size.x, b.size.z) * 0.72f, 0.0f, 4, b.roof);
        }
        if (b.tavern)   // hanging sign: a keg (or the lord's banner on a keep)
            DrawSphere({ b.pos.x, b.size.y + 3.4f, b.pos.z }, 0.7f, GOLD);
    }

    for (const Npc& n : T.npcs) {
        Pose pose;
        pose.yaw = n.yaw;
        pose.walkPhase = n.walkPhase;
        DrawCharacter(c, n.pos, n.loadout, pose, BEIGE);
    }

    Pose hero;
    hero.yaw = T.yaw;
    hero.walkPhase = T.walkPhase;
    DrawCharacter(c, T.pPos, gs.playerHero.loadout, hero, Color{ 40, 120, 255, 255 });
    EndMode3D();

    // ---- HUD ----
    DrawRectangle(0, 0, GetScreenWidth(), 34, Fade(BLACK, 0.6f));
    ui::Text(TextFormat("%s  ·  Gold: %d   Party: %d", town.name.c_str(), gs.gold,
                        gs.player.totalTroops()), 10, 8, 20, RAYWHITE);

    if (TownAtTavern()) {
        const std::vector<int>& roster = c.factions[c.playerFaction].roster;
        const int y = GetScreenHeight() - 60 - (int)roster.size() * 24;
        DrawRectangle(0, y - 8, GetScreenWidth(), GetScreenHeight() - y + 8, Fade(BLACK, 0.7f));
        ui::Text("The tavern. Recruits wait for coin:", 10, y, 20, GOLD);
        for (int slot = 0; slot < (int)roster.size(); ++slot) {
            const TroopDef& td = c.troops[roster[slot]];
            ui::Text(TextFormat("[%d] %s - %d gold  (have %d)", slot + 1, td.name.c_str(),
                                td.cost, gs.player.troopCounts[roster[slot]]),
                     10, y + 26 + slot * 24, 20, RAYWHITE);
        }
    } else {
        ui::Text("WASD walk, mouse look. The gold roof is the tavern. Esc leaves.",
                 10, GetScreenHeight() - 26, 16, Fade(RAYWHITE, 0.7f));
    }
    EndDrawing();
}
