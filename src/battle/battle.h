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

    // Who stands against you (V24): named at the horn — a lord, a crown's
    // war-band, or a garrison. Presentation only.
    std::string enemyName;
    // The lord himself (V101): when set, he takes the field in person — a
    // champion among his men, and a prize.
    std::string enemyLordName;

    // The destrier (V82): the hero's horse carries double hit points.
    bool warhorse = false;
    // A crowned head wears its circlet into battle (V94). Presentation only.
    bool crowned = false;

    // Hungry men fight shaken (V37): true when the warband has marched
    // without bread — the player's own line starts with rattled nerve.
    bool hungry = false;

    // The hero's body (V14): Strength scales swing damage (+5%/pt),
    // Agility his footwork (+2%/pt move speed). Read-only snapshot.
    int heroStr = 0;
    int heroAgi = 0;

    // Time of day (O3): the campaign clock's day fraction (0..1) — night
    // battles fight under a dark sky, dusk under amber. Presentation only.
    float timeOfDay = 0.3f;

    // Siege engineering (N1): what the attackers built before the assault.
    // 0 = storm now (gate + the two standing ladders), 1 = built ladders
    // (two more climbing points), 2 = a siege tower (a wide rolling lane
    // as well). Costs days on the campaign side; the garrison musters on.
    int siegePrep = 0;

    // The storm (V62): -1 = let the terrain seed decide the weather (as
    // ever), 0 = force dry, 1 = force rain (fighting inside the cell).
    int rainOverride = -1;

    // World biome at the battlefield (K8): computed by the orchestrator from
    // the moddable map so battle terrain follows a modded biome without this
    // module knowing the world. Negative = derive from campaignPos as before.
    float hilliness     = -1.0f;     // 0..1
    float forestDensity = -1.0f;     // 0..1

    // Siege assaults (roadmap B3b): towns and castles defend from behind a
    // wall with a single gate; villages are open raids.
    // Fortified (V51/V52): wall-work paid for on the campaign side — the
    // wall archers shoot faster from a better platform.
    bool           fortified = false;
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
    int              horsesTaken = 0;   // masterless mounts on a won field (V22)
    bool             slewLord = false;  // the enemy lord fell to ANYONE's hand
                                        // on a won field (V101)
    std::vector<int> enemySurrendered;  // men who yielded, per troop (V42) —
                                        // the campaign takes them prisoner
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
    const char* order = "Charge";   // current battlefield order (M2)
    const char* formation = "Charge";   // current formation shape (V48)
    int     climbPoints = 0;        // siege climbing lanes incl. tower (N1)
    bool    raining = false;        // wet strings throw short (R1)
    bool    night   = false;        // darkness shortens bowshot (V44)
    int     heroKills = 0;          // kills by the hero's own hand
    int     looseHorses = 0;        // mounts that outlived their riders (T6)
    std::string enemyName;          // who the banner named at the horn (V24)
    int     heroKicks = 0;          // boots landed (V33)
    float   heroShieldHp = 0;       // wood left on the hero's arm (V71)
    int     reservesOwn   = 0;      // men waiting off-field (V75)
    int     reservesEnemy = 0;
    bool    dueling = false;        // single combat holds the field (V102)
    bool    bannerOwn   = false;    // our standard still flies (V32)
    bool    bannerEnemy = false;    // theirs still flies
    float   ownAvgDistToAnchor = 0; // player troops' mean distance from their
                                    // order anchor — lets scripts see obedience
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
