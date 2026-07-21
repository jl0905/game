#pragma once
#include "../content.h"
#include "../input.h"
#include <vector>

// ---------------------------------------------------------------------------
// Battle module public interface — the ONLY handoff between the world map and
// a battle. The battle never sees campaign state; it receives a snapshot of
// what matters in BattleSetup and reports what happened in BattleOutcome.
//
// Like the campaign, the battle is split so it can be driven programmatically:
//   GatherBattleInput — real devices → BattleInput (windowed only)
//   BattleUpdate      — pure simulation, no drawing / device reads
//   BattleDraw        — render the current state (windowed only)
//   GetBattleView     — read-only snapshot for harnesses / debugging
// ---------------------------------------------------------------------------

// Everything a battle needs, captured from the world map at the moment two
// parties engage.
struct BattleSetup {
    std::vector<int> playerTroops;   // counts, parallel to Content::troops
    std::vector<int> enemyTroops;    // counts, parallel to Content::troops
    std::vector<int> allyTroops;     // a friendly party fighting alongside you
                                     // (empty when you fight alone)
    Loadout          heroLoadout;    // the player avatar's equipment
    int              heroMaxHp = 0;

    // Per-troop gear overrides (K6): companions fitted from the player's bag
    // fight in that gear instead of their catalogue loadout. Pairs of
    // (troop handle, loadout); troops not listed use the content default.
    std::vector<std::pair<int, Loadout>> gearOverrides;
    Vector2          campaignPos{};  // where on the world map this fight happens
                                     // (drives terrain generation)

    // World biome at the battlefield (K8): computed by the orchestrator from
    // the moddable map so battle terrain follows a modded biome without this
    // module knowing the world. Negative = derive from campaignPos as before.
    float hilliness     = -1.0f;     // 0..1
    float forestDensity = -1.0f;     // 0..1

    // Siege assaults (roadmap B3b): towns and castles defend from behind a
    // wall with a single gate; villages are open raids.
    bool           siege = false;
    SettlementType siegeType = SettlementType::Village;

    // Tournament bout (direction G2): a flat sanded ring — no weather, no
    // mounts, no battle lines; both sides are borrowed arena fighters and the
    // outcome must not touch real troops (the campaign handles the purse).
    bool arena = false;
};

// What the battle reports back once it ends.
struct BattleOutcome {
    bool             won = false;
    std::vector<int> playerLosses;   // parallel to Content::troops
    std::vector<int> allyLosses;     // losses among allyTroops (empty if none)
    std::vector<int> enemyLosses;    // enemy dead, parallel to Content::troops
};

// Read-only view of the running battle, for script harnesses and debugging.
struct BattleView {
    bool    active = false;
    Vector3 heroPos{};
    float   heroYaw = 0, heroPitch = 0;
    float   heroHp = 0, heroMaxHp = 0;
    int     heroWeapon = -1;   // active weapon handle
    int     aliveAllies = 0, aliveEnemies = 0;
    int     arrowsInFlight = 0;
    int     wallDefenders = 0;   // live garrison archers still on the wall
    bool    heroMounted = false;
    float   heroHorseHp = 0;
    bool    over = false, won = false;
};

// Start a battle from a world-map snapshot.
void BattleInit(const Content& c, const BattleSetup& setup);

// Real devices → battle intent. Windowed play only.
BattleInput GatherBattleInput();

// Advance one frame of simulation. Returns true while the battle is ongoing;
// returns false exactly once when it ends, with `out` filled in.
bool BattleUpdate(const Content& c, float dt, const BattleInput& in, BattleOutcome& out);

// Render the current battle state. Call only between BattleInit and the end.
void BattleDraw(const Content& c);

BattleView GetBattleView();
