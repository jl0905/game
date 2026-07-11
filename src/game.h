#pragma once
#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <string>

// ---------- Shared game state ----------

enum class Screen { Campaign, Battle, BattleResult };

struct TroopType {
    const char* name;
    int hp;
    float damage;
    float speed;
    int cost;       // recruit cost in gold
    Color color;
};

// index into TROOP_TYPES
enum { TROOP_RECRUIT = 0, TROOP_INFANTRY = 1, TROOP_VETERAN = 2, TROOP_COUNT = 3 };
extern const TroopType TROOP_TYPES[TROOP_COUNT];

struct Party {
    Vector2 pos;            // campaign map position
    int troops[TROOP_COUNT] = {0, 0, 0};
    bool hostile = false;
    bool alive = true;
    Vector2 wanderTarget{};
    float thinkTimer = 0;

    int totalTroops() const { return troops[0] + troops[1] + troops[2]; }
};

struct Town {
    Vector2 pos;
    const char* name;
};

struct GameState {
    Screen screen = Screen::Campaign;

    // Campaign
    Party player;
    std::vector<Party> enemies;
    std::vector<Town> towns;
    int gold = 300;
    float enemySpawnTimer = 0;
    int nearTown = -1;      // index of town in range, or -1

    // Battle handoff
    int battleEnemyIndex = -1;
    bool battleWon = false;
    int playerLosses[TROOP_COUNT] = {0, 0, 0};
    std::string resultText;
};

// campaign.cpp
void CampaignInit(GameState& gs);
void CampaignUpdateDraw(GameState& gs, float dt);

// battle.cpp
void BattleInit(GameState& gs);
void BattleUpdateDraw(GameState& gs, float dt);
