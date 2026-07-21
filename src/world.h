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
    bool             caravan = false;    // trade convoy plying owned settlements (E3)
    int              caravanTo = -1;     // destination town index while trading
    Vector2          wanderTarget{};
    float            thinkTimer = 0;

    // Movement state (observational — see PartyState). `fatigue` grows while a
    // party marches and drains while it rests at a friendly settlement; a tired
    // party breaks off whatever it was doing and rides home to camp.
    PartyState       state    = PartyState::Patrolling;
    float            fatigue  = 0;
    int              restTown = -1;   // settlement being rested at, or -1

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

    // Prosperity (direction E3): percent scale on this settlement's daily
    // income, fattened by arriving caravans. TODO(balance): growth/cap.
    int prosperity = 100;

    // Marketplace (direction E1), parallel to Content::goods. `priceOffset` is
    // a percentage of GoodDef::basePrice (100 = base) — per-town spreads are
    // the structural hook for prosperity/caravans; values stay flat for now.
    std::vector<int> stock;        // units for sale
    std::vector<int> priceOffset;  // TODO(balance): percent of base price

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

// How many trade-good units the party's saddlebags hold in total (E2).
// TODO(balance): scale with party size / a pack-horse upgrade later.
inline constexpr int GOODS_CAP = 30;

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
    std::vector<int>   prisoners;                // captives by troop type (ransomable)
    std::vector<InvItem> inventory;              // hero's tiled bag (D1)
    std::vector<int>     goods;                  // trade goods held, per good type (E1)
    std::vector<int>     enterpriseAt;           // enterprise handle per town, -1 none (E4)
    int                invCarry = -1;            // inventory item being moved (transient)

    // Active quest (F4): one at a time. `questTown` is the delivery target
    // for DeliverGrain; `questFaction` earns the relation reward.
    int activeQuest   = -1;   // quest handle, -1 none
    int questFaction  = -1;   // the giver's faction
    int questTown     = -1;   // delivery destination (DeliverGrain)
    int questProgress = 0;    // parties broken so far (HuntBandits)

    // Vassalage (F2): the kingdom the player has sworn to, or -1 while a free
    // captain. Swearing aligns the player's wars with the liege's and grants
    // a village fief. TODO(balance): oath requirements, muster obligations.
    int liege = -1;

    // Relations (F1): the player's personal standing with each faction,
    // -100..100, moved by deeds (battles, raids, aid). Report-only for now;
    // vassalage (F2) and quest givers (F4) will read it. TODO(balance).
    std::vector<int> relations;

    // Live diplomacy (C4): a runtime copy of Content::hostile. Wars between
    // kingdoms (factions that field lords, plus the player) accumulate
    // weariness from casualties, end in a truce, and rekindle when it lapses.
    // Outlaw factions never treat. All flat numbers are TODO(balance).
    std::vector<unsigned char> hostile;     // factions × factions, 1 = at war
    std::vector<int>           warScore;    // casualties this war, per pair
    std::vector<float>         truceDays;   // days of peace left, per pair

    // Battle handoff
    bool             arenaFight = false;         // tournament bout, not a real battle (G2)
    int              siegeTownIndex   = -1;      // assaulting this town (else -1)
    int              battlePartyIndex = -1;      // enemy: index into `parties`
    int              battleAllyIndex  = -1;      // friendly party joining you, or -1
    bool             battleWon = false;
    std::vector<int> playerLosses;               // parallel to troops
    std::vector<int> allyLosses;                 // parallel to troops (if an ally fought)
    std::vector<int> enemyLosses;                // enemy dead, for the battle report
    std::string      resultText;

    // Aftermath card: filled when a battle result is applied, drawn as a
    // fading panel over the map for a few seconds. Purely presentational.
    std::vector<std::string> battleReport;
    float                    battleReportTimer = 0;
};

// A sworn vassal fights his liege's wars: copy the liege's stance toward every
// third faction onto the player, and keep the peace between the two. Called
// when the oath is sworn and every world day (diplomacy shifts under it).
inline void AlignWarsWithLiege(GameState& gs) {
    const int n = gs.content.factions.size();
    const int p = gs.content.playerFaction, L = gs.liege;
    if (L < 0 || L >= n || p < 0 || (int)gs.hostile.size() != n * n) return;
    for (int f = 0; f < n; ++f) {
        if (f == p || f == L) continue;
        gs.hostile[(size_t)p * n + f] = gs.hostile[(size_t)f * n + p] =
            gs.hostile[(size_t)L * n + f];
    }
    gs.hostile[(size_t)p * n + L] = gs.hostile[(size_t)L * n + p] = 0;
}

// Whether two factions currently fight — the runtime diplomacy matrix when it
// exists, falling back to the static Content relations. Use this (not
// AreFactionsHostile) for anything happening in a live world.
inline bool AtWar(const GameState& gs, int a, int b) {
    const int n = gs.content.factions.size();
    if (a < 0 || b < 0 || a == b) return AreFactionsHostile(gs.content, a, b);
    if ((int)gs.hostile.size() == n * n) return gs.hostile[(size_t)a * n + b] != 0;
    return AreFactionsHostile(gs.content, a, b);
}
