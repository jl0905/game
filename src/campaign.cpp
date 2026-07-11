#include "game.h"
#include <cstdio>
#include <cstdlib>

// Mount & Blade style campaign map: top-down world, your party moves toward
// the mouse cursor while holding LMB (or with WASD). Enemy war parties roam
// and chase you; touching one starts a battle. Enter a town to recruit.

const TroopType TROOP_TYPES[TROOP_COUNT] = {
    { "Recruit",  40, 12.0f, 5.2f, 10, LIGHTGRAY },
    { "Infantry", 70, 20.0f, 5.0f, 30, SKYBLUE },
    { "Veteran", 110, 30.0f, 5.4f, 80, GOLD },
};

static const float MAP_SIZE = 2000.0f;
static const float PARTY_SPEED = 160.0f;

static float frand(float a, float b) {
    return a + (b - a) * ((float)GetRandomValue(0, 10000) / 10000.0f);
}

static Party MakeEnemyParty(int strength) {
    Party p;
    p.hostile = true;
    p.pos = { frand(200, MAP_SIZE - 200), frand(200, MAP_SIZE - 200) };
    p.troops[TROOP_RECRUIT]  = 3 + strength;
    p.troops[TROOP_INFANTRY] = strength / 2;
    p.troops[TROOP_VETERAN]  = strength / 4;
    p.wanderTarget = p.pos;
    return p;
}

void CampaignInit(GameState& gs) {
    gs.player.pos = { MAP_SIZE / 2, MAP_SIZE / 2 };
    gs.player.troops[TROOP_RECRUIT] = 5;
    gs.player.troops[TROOP_INFANTRY] = 2;
    gs.player.troops[TROOP_VETERAN] = 0;

    gs.towns = {
        { { 400, 400 },   "Sargoth" },
        { { 1600, 500 },  "Praven" },
        { { 500, 1550 },  "Tulga" },
        { { 1500, 1500 }, "Jelkala" },
    };
    gs.enemies.clear();
    for (int i = 0; i < 5; i++) gs.enemies.push_back(MakeEnemyParty(2 + i));
}

static void ApplyBattleResult(GameState& gs) {
    for (int t = 0; t < TROOP_COUNT; t++) {
        gs.player.troops[t] -= gs.playerLosses[t];
        if (gs.player.troops[t] < 0) gs.player.troops[t] = 0;
    }
    if (gs.battleWon) {
        int loot = 50 + GetRandomValue(0, 100);
        gs.gold += loot;
        gs.resultText = TextFormat("VICTORY!  Loot: %d gold", loot);
        if (gs.battleEnemyIndex >= 0 && gs.battleEnemyIndex < (int)gs.enemies.size())
            gs.enemies[gs.battleEnemyIndex].alive = false;
    } else {
        gs.resultText = "DEFEAT...  You escape with the survivors.";
        // knock the player away so we don't instantly re-collide
        gs.player.pos.x = Clamp(gs.player.pos.x + frand(-300, 300), 100, MAP_SIZE - 100);
        gs.player.pos.y = Clamp(gs.player.pos.y + frand(-300, 300), 100, MAP_SIZE - 100);
    }
    gs.battleEnemyIndex = -1;
}

void CampaignUpdateDraw(GameState& gs, float dt) {
    // ---- returning from a battle? ----
    if (gs.screen == Screen::BattleResult) {
        ApplyBattleResult(gs);
        gs.screen = Screen::Campaign;
    }

    // ---- player movement ----
    Vector2 move = { 0, 0 };
    if (IsKeyDown(KEY_W)) move.y -= 1;
    if (IsKeyDown(KEY_S)) move.y += 1;
    if (IsKeyDown(KEY_A)) move.x -= 1;
    if (IsKeyDown(KEY_D)) move.x += 1;

    Camera2D cam = { 0 };
    cam.target = gs.player.pos;
    cam.offset = { GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f };
    cam.zoom = 1.0f;

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

    // ---- towns: recruiting ----
    gs.nearTown = -1;
    for (int i = 0; i < (int)gs.towns.size(); i++) {
        if (Vector2Distance(gs.player.pos, gs.towns[i].pos) < 60) gs.nearTown = i;
    }
    if (gs.nearTown >= 0) {
        for (int t = 0; t < TROOP_COUNT; t++) {
            if (IsKeyPressed(KEY_ONE + t) && gs.gold >= TROOP_TYPES[t].cost) {
                gs.gold -= TROOP_TYPES[t].cost;
                gs.player.troops[t]++;
            }
        }
    }

    // ---- enemy AI: wander, chase player if weaker ----
    int playerStrength = gs.player.totalTroops();
    for (auto& e : gs.enemies) {
        if (!e.alive) continue;
        e.thinkTimer -= dt;
        float distToPlayer = Vector2Distance(e.pos, gs.player.pos);
        Vector2 target = e.wanderTarget;

        if (distToPlayer < 350 && e.totalTroops() >= playerStrength / 2) {
            target = gs.player.pos;   // chase
        } else if (e.thinkTimer <= 0) {
            e.wanderTarget = { frand(100, MAP_SIZE - 100), frand(100, MAP_SIZE - 100) };
            e.thinkTimer = frand(3, 8);
            target = e.wanderTarget;
        }
        Vector2 dir = Vector2Subtract(target, e.pos);
        if (Vector2Length(dir) > 5)
            e.pos = Vector2Add(e.pos, Vector2Scale(Vector2Normalize(dir), PARTY_SPEED * 0.7f * dt));

        // collision -> battle!
        if (Vector2Distance(e.pos, gs.player.pos) < 24 && gs.player.totalTroops() > 0) {
            gs.battleEnemyIndex = (int)(&e - &gs.enemies[0]);
            gs.screen = Screen::Battle;
            BattleInit(gs);
            return;
        }
    }

    // ---- respawn enemies over time ----
    gs.enemySpawnTimer += dt;
    int aliveCount = 0;
    for (auto& e : gs.enemies) if (e.alive) aliveCount++;
    if (gs.enemySpawnTimer > 20 && aliveCount < 8) {
        gs.enemySpawnTimer = 0;
        gs.enemies.push_back(MakeEnemyParty(GetRandomValue(2, 6)));
    }

    // ================= DRAW =================
    BeginDrawing();
    ClearBackground(Color{ 60, 92, 48, 255 });   // grass

    BeginMode2D(cam);
    // map border + decorative grid
    DrawRectangleLinesEx(Rectangle{ 0, 0, MAP_SIZE, MAP_SIZE }, 6, DARKBROWN);
    for (int x = 0; x <= (int)MAP_SIZE; x += 200)
        DrawLine(x, 0, x, (int)MAP_SIZE, Color{ 70, 102, 58, 255 });
    for (int y = 0; y <= (int)MAP_SIZE; y += 200)
        DrawLine(0, y, (int)MAP_SIZE, y, Color{ 70, 102, 58, 255 });

    for (int i = 0; i < (int)gs.towns.size(); i++) {
        const Town& t = gs.towns[i];
        DrawRectangle((int)t.pos.x - 20, (int)t.pos.y - 20, 40, 40, BROWN);
        DrawTriangle({ t.pos.x - 24, t.pos.y - 20 }, { t.pos.x + 24, t.pos.y - 20 },
                     { t.pos.x, t.pos.y - 44 }, MAROON);
        DrawText(t.name, (int)t.pos.x - 30, (int)t.pos.y + 26, 16, RAYWHITE);
        DrawCircleLines((int)t.pos.x, (int)t.pos.y, 60, Fade(RAYWHITE, 0.25f));
    }

    for (auto& e : gs.enemies) {
        if (!e.alive) continue;
        DrawCircleV(e.pos, 12, RED);
        DrawText(TextFormat("%d", e.totalTroops()), (int)e.pos.x - 6, (int)e.pos.y - 32, 16, RED);
    }

    DrawCircleV(gs.player.pos, 12, BLUE);
    DrawText("You", (int)gs.player.pos.x - 12, (int)gs.player.pos.y - 34, 16, RAYWHITE);
    EndMode2D();

    // ---- HUD ----
    DrawRectangle(0, 0, GetScreenWidth(), 34, Fade(BLACK, 0.6f));
    DrawText(TextFormat("Gold: %d   Party: %d Recruits, %d Infantry, %d Veterans",
                        gs.gold, gs.player.troops[0], gs.player.troops[1], gs.player.troops[2]),
             10, 8, 20, RAYWHITE);

    if (gs.nearTown >= 0) {
        int y = GetScreenHeight() - 110;
        DrawRectangle(0, y, GetScreenWidth(), 110, Fade(BLACK, 0.7f));
        DrawText(TextFormat("Welcome to %s! Recruit troops:", gs.towns[gs.nearTown].name), 10, y + 8, 20, GOLD);
        for (int t = 0; t < TROOP_COUNT; t++) {
            DrawText(TextFormat("[%d] %s - %d gold", t + 1, TROOP_TYPES[t].name, TROOP_TYPES[t].cost),
                     10, y + 34 + t * 24, 20, RAYWHITE);
        }
    } else if (!gs.resultText.empty()) {
        DrawText(gs.resultText.c_str(), 10, 42, 20, GOLD);
    }
    DrawText("Move: WASD or hold LMB. Red parties chase you. Towns recruit troops.",
             10, GetScreenHeight() - (gs.nearTown >= 0 ? 134 : 28), 16, Fade(RAYWHITE, 0.7f));

    if (gs.player.totalTroops() == 0 && gs.gold < TROOP_TYPES[0].cost) {
        DrawText("Your warband is destitute... visit a town or press R to restart.",
                 10, 70, 20, RED);
        if (IsKeyPressed(KEY_R)) { GameState fresh; gs = fresh; CampaignInit(gs); }
    }

    EndDrawing();
}
