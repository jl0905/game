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
};

// A party moving on the campaign map. Its troop composition is stored per troop
// type, parallel to Content::troops.
struct Party {
    Vector2          pos{};
    int              faction = -1;      // FactionDef handle
    std::vector<int> troopCounts;       // one entry per troop type
    bool             isPlayer = false;
    bool             alive = true;
    Vector2          wanderTarget{};
    float            thinkTimer = 0;

    int totalTroops() const {
        int n = 0;
        for (int c : troopCounts) n += c;
        return n;
    }
};

struct Town {
    Vector2     pos{};
    std::string name;
};

// The single mutable game state passed to every subsystem.
struct GameState {
    Screen  screen = Screen::Campaign;
    Content content;

    // Campaign
    Character          playerHero;   // the avatar you control in battle
    Party              player;
    std::vector<Party> parties;      // all non-player parties
    std::vector<Town>  towns;
    int                gold = 300;
    float              spawnTimer = 0;
    int                nearTown = -1;

    // Battle handoff
    int              battlePartyIndex = -1;      // index into `parties`
    bool             battleWon = false;
    std::vector<int> playerLosses;               // parallel to troops
    std::string      resultText;
};
