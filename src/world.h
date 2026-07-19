#pragma once
#include "content.h"
#include "raylib.h"
#include "raymath.h"
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Runtime world state (as opposed to the static Content catalogue).
// ---------------------------------------------------------------------------

// A hero character sheet — currently the player avatar, but reusable for named
// lords/companions later. Equipment lives in the loadout; swap handles to swap
// the models and (once balanced) the stats.
struct Character {
    Loadout loadout;
    int     maxHp = 100;  // placeholder base; see content.h note on balance
    float   hp    = 100;

    // Progression (roadmap D3): level/XP and attribute values. Attributes are
    // parallel to Content::attributes; effects arrive with the balance pass.
    int              level = 1;
    int              xp = 0;
    int              attrPoints = 0;
    std::vector<int> attributes;
};

// A party moving on the campaign map. Its troop composition is stored per troop
// type, parallel to Content::troops.
struct Party {
    Vector2          pos{};
    int              faction = -1;      // FactionDef handle
    std::vector<int> troopCounts;       // one entry per troop type
    bool             isPlayer = false;
    bool             alive = true;
    bool             engaged = false;    // locked in a world-map skirmish/siege
    std::string      lord;               // lord's name; empty for ordinary parties
    Vector2          wanderTarget{};
    float            thinkTimer = 0;

    int totalTroops() const {
        int n = 0;
        for (int c : troopCounts) n += c;
        return n;
    }
};

struct Town {
    Vector2        pos{};
    std::string    name;
    SettlementType type  = SettlementType::Town;
    int            owner = -1;   // owning faction handle; changes at runtime (sieges)
    std::vector<int> garrison;   // defending troops, parallel to Content::troops

    int garrisonSize() const {
        int n = 0;
        for (int c : garrison) n += c;
        return n;
    }
};

// An item lying in the hero's tiled inventory (roadmap D1). Items are content
// handles; (x, y) is the top-left cell of its tileW×tileH footprint.
struct InvItem {
    bool isWeapon = false;
    int  handle   = -1;
    int  x = 0, y = 0;
};

inline constexpr int INV_W = 10;   // inventory grid size, in cells
inline constexpr int INV_H = 6;

// An AI army besieging a settlement; resolves on its own after `timer`.
struct AISiege {
    int   party = -1;   // attacker, index into GameState::parties
    int   town  = -1;   // target, index into GameState::towns
    float timer = 0;
};

// A fallen lord gathering a new host; respawns at an owned settlement.
struct LordRespawn {
    int         faction = -1;
    std::string name;
    float       timer = 0;
};

// Two hostile AI parties locked in a fight on the world map. It resolves on its
// own after `timer` runs out (the player can watch), or the player can join a
// side and turn it into a full battle. `a` and `b` index into GameState::parties.
struct Skirmish {
    int     a = -1;
    int     b = -1;
    float   timer    = 0;   // seconds left until auto-resolution
    float   duration = 1;   // starting timer, for drawing progress
    Vector2 pos{};          // midpoint where the clash is drawn
};

// The single mutable game state passed to every subsystem.
struct GameState {
    Screen  screen = Screen::Campaign;
    Content content;

    // Campaign
    Character          playerHero;   // the avatar you control in battle
    Party              player;
    std::vector<Party>    parties;      // all non-player parties
    std::vector<Town>     towns;
    std::vector<Skirmish> skirmishes;   // ongoing AI-vs-AI clashes on the map
    std::vector<AISiege>  aiSieges;     // AI armies besieging settlements
    std::vector<LordRespawn> lordRespawns;   // fallen lords raising new hosts
    int                gold = 300;
    int                day = 0;          // world days elapsed (economy ticks)
    float              dayTimer = 0;
    float              spawnTimer = 0;
    int                nearTown = -1;
    int                currentSettlement = -1;   // town index while inside a settlement
    bool               timeFlowing = false;      // did world time advance this frame?
    std::vector<int>   troopXp;                  // player XP pool per troop type (C2)
    std::vector<InvItem> inventory;              // hero's tiled bag (D1)
    int                invCarry = -1;            // inventory item being moved (transient)

    // Battle handoff
    int              siegeTownIndex   = -1;      // assaulting this town (else -1)
    int              battlePartyIndex = -1;      // enemy: index into `parties`
    int              battleAllyIndex  = -1;      // friendly party joining you, or -1
    bool             battleWon = false;
    std::vector<int> playerLosses;               // parallel to troops
    std::vector<int> allyLosses;                 // parallel to troops (if an ally fought)
    std::vector<int> enemyLosses;                // enemy dead, for the battle report
    std::string      resultText;
};
