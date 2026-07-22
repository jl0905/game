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
    std::vector<int> cargo;              // freight per good — real wares moved
                                         // between town markets, spilt on plunder
    int              cargoCost = 0;      // what the load cost at its origin (M4):
                                         // a player convoy's profit is revenue - this
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

    // Recruit pool (V2): the settlement's sons, fed by prosperity — a rich
    // town raises more spears; a raided one has none to give. Pool cap is
    // prosperity/25, refilling one a day. TODO(balance).
    int recruitPool = 4;

    // Fief grant (M3): the raised lord who holds this seat for the crowned
    // player — he taxes it (its income skips the ledger) and respawns here.
    std::string fiefLord;

    // Marketplace (direction E1), parallel to Content::goods. `priceOffset` is
    // a percentage of GoodDef::basePrice (100 = base) — per-town spreads are
    // the structural hook for prosperity/caravans; values stay flat for now.
    std::vector<int> stock;        // units for sale
    std::vector<int> priceOffset;  // TODO(balance): percent of base price

    // War economy (V31): percent markup on every price while the owner is
    // at war — markets learn the news by morning (refreshed at dawn).
    int warMarkup = 100;

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

// A bandit den on the map (H2): breeds parties of its faction until the
// player storms it. `days` accumulates toward the next spawn.
struct Lair {
    Vector2 pos{};
    int     faction = -1;
    bool    alive = true;
    float   days = 0;
};

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
    std::vector<Lair>     lairs;        // bandit dens breeding parties (H2)
    int                   lairBattle = -1;   // lair being stormed, or -1
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
    int                  invTarget = 0;          // who the bag fits: 0 hero, 1.. hired companions (K6)
    std::vector<std::pair<int, Loadout>> companionGear;   // per-companion fitted gear (K6)
    std::vector<int>     goods;                  // trade goods held, per good type (E1)
    std::vector<int>     bankAt;                 // gold on deposit per town (V5):
                                                 // earns weekly interest, and banked
                                                 // capital feeds local prosperity
    std::vector<int>     enterpriseAt;           // enterprise handle per town, -1 none (E4)
    int                invCarry = -1;            // inventory item being moved (transient)

    // Where the settings screen returns to when it closes (K1).
    Screen settingsBack = Screen::Title;

    // Dialogue screen (H4): who the player is talking to and what they said.
    // Filled by the town scene when a conversation opens; transient.
    std::string              dialogueName;
    std::vector<std::string> dialogueLines;
    bool                     dialogueLord = false;   // court audience (K2): oath/work topics
    int                      parleyParty  = -1;      // lord party in a map parley (S4);
                                                     // -1 = a settlement audience
    std::string              audienceLord;           // the named lord in audience (V26);
                                                     // empty = a castellan/villager

    // The commissary (V37): the warband eats one grain per 20 men (rounded
    // up) at every dawn, from the saddlebags. Days without bread stack;
    // from the second one, men start walking home and the line marches
    // into battle with shaken nerve.
    int hungryDays = 0;

    // Mercenary contract (V29): a hired company's party index and the days
    // left on its papers. It shadows the player's march and joins battles
    // within reach as the allied line.
    int   mercParty = -1;
    float mercDays  = 0;

    // Active quest (F4): one at a time. `questTown` is the delivery target
    // for DeliverGrain; `questFaction` earns the relation reward.
    int activeQuest   = -1;   // quest handle, -1 none
    int questFaction  = -1;   // the giver's faction
    int questTown     = -1;   // delivery destination (DeliverGrain)
    int questProgress = 0;    // parties broken so far (HuntBandits)

    // Player kingdom (F3): true once the player has claimed a crown. Crowning
    // needs two settlements, turns every other crown hostile, and unlocks
    // raising lords at owned settlements. TODO(balance): every requirement.
    bool crowned = false;

    // Vassalage (F2): the kingdom the player has sworn to, or -1 while a free
    // captain. Swearing aligns the player's wars with the liege's and grants
    // a village fief. TODO(balance): oath requirements, muster obligations.
    int liege = -1;

    // Muster (K5): the liege's summons to a siege — answer it by riding to
    // the invested town before the days run out, or pay in standing. And the
    // crown's rally: a crowned ruler calls raised lords to the banner.
    int     musterTown = -1;   // besieged town you are summoned to, -1 none
    float   musterDays = 0;    // days left to answer
    bool    lordsRally = false;
    Vector2 lordsRallyPos{};
    float   lordsRallyDays = 0;

    // Feasts & marriage (M5): a kingdom at peace throws a feast at one of
    // its towns for a few days; attending pays standing and renown, and a
    // feast's court is where marriages are made. One spouse, ever — the
    // alliance floors your standing with that house at 0.
    int   feastTown     = -1;   // town index hosting, -1 none
    int   feastFaction  = -1;   // the celebrating crown
    float feastDays     = 0;    // days of feasting left
    bool  feastAttended = false;
    int         spouseFaction = -1;
    std::string spouseName;

    // Renown & honor (M1): fame from victories/tournaments/quests, and a
    // conscience moved by deeds. Renown gates vassalage and raises the
    // party cap; honor is report-only until per-lord opinions exist.
    // All thresholds flat TODO(balance).
    int renown = 0;
    int honor  = 0;

    // The wounded cart (S3): half a battle's losses ride behind the warband
    // and rejoin as they heal (one per troop type per dawn, cap respected).
    std::vector<int> wounded;   // parallel to Content::troops

    // First-hour hints (P4): one-time toasts, a bit each once shown.
    unsigned hintsSeen = 0;   // 1 first victory, 2 first captives, 4 first loot

    // Prisoner lords (O2): named lords captured when you break their host.
    // Ransomed back to their crown for gold (they resent it) or released
    // (honor, gratitude); a captive does not respawn until freed.
    std::vector<std::pair<std::string, int>> capturedLords;   // name, faction

    // Per-lord opinion (N4): named lords remember what you did to and for
    // them — grants, battles at their side, defeats at your hand. Honor is
    // added on top when the opinion is *read* (honest men are trusted a
    // little everywhere). All deltas flat TODO(balance).
    std::vector<std::pair<std::string, int>> lordOpinion;

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

    // Siege engineering (N1): the assault choice at a walled settlement,
    // and the camp where engines are built (days pass, garrisons muster on).
    int   siegePrompt   = -1;   // walled town awaiting the choice (transient)
    int   siegeCampTown = -1;   // camped outside this town building engines
    int   siegeCampPrep = 0;    // what is being built: 1 ladders, 2 tower
    float siegeCampDays = 0;    // build days left
    int   siegeLaunchPrep = 0;  // engines of the assault being fought (transient)
    bool  raidingVillage  = false;   // this village fight is a raid, not a
                                     // conquest (P1; transient)

    // Battle handoff
    bool             arenaFight = false;         // tournament bout, not a real battle (G2)
    int              arenaRound = 0;             // 1..3 while a bracket runs (K3)
    int              arenaBet   = 0;             // gold staked on yourself (K3)
    int              siegeTownIndex   = -1;      // assaulting this town (else -1)
    int              battlePartyIndex = -1;      // enemy: index into `parties`
    int              battleAllyIndex  = -1;      // friendly party joining you, or -1
    bool             battleWon = false;
    std::vector<int> playerLosses;               // parallel to troops
    std::vector<int> allyLosses;                 // parallel to troops (if an ally fought)
    std::vector<int> enemyLosses;                // enemy dead, for the battle report
    int              battleHorses = 0;           // strays rounded up on a won field (V22)
    std::string      resultText;

    // Aftermath card: filled when a battle result is applied, drawn as a
    // fading panel over the map for a few seconds. Purely presentational.
    std::vector<std::string> battleReport;
    float                    battleReportTimer = 0;
};

// ---------------------------------------------------------------------------
// World terrain (travel-speed pass): the same noise that paints the map and
// seeds battlefields (TerrainConfigFromWorld in battle.cpp) classifies any
// point, so what you see is what slows you. Thresholds match the map paint.
// ---------------------------------------------------------------------------

enum class WorldTerrain { Plains, Forest, Mountain };

// The biome noise itself (K8): parameters come from the moddable map.
inline float BiomeHillNoise(const MapDef& m, Vector2 p) {
    return sinf(p.x * m.biome.hillFreqX) * cosf(p.y * m.biome.hillFreqY);
}
inline float BiomeForestNoise(const MapDef& m, Vector2 p) {
    return sinf(p.x * m.biome.forestFreqX + p.y * m.biome.forestFreqY);
}

inline WorldTerrain WorldTerrainAt(const MapDef& m, Vector2 p) {
    if (BiomeForestNoise(m, p) > m.biome.forestThreshold) return WorldTerrain::Forest;
    if (BiomeHillNoise(m, p) > m.biome.mountainThreshold) return WorldTerrain::Mountain;
    return WorldTerrain::Plains;
}

inline const char* WorldTerrainName(WorldTerrain t) {
    switch (t) {
        case WorldTerrain::Forest:   return "forest";
        case WorldTerrain::Mountain: return "mountain";
        default:                     return "plains";
    }
}

// Roads join settlements closer than MapDef::roadLinkDist (the drawn
// network); within roadWidth of a link a party travels at full pace.
inline bool OnRoad(const GameState& gs, Vector2 p) {
    const MapDef& m = gs.content.map;
    for (int a = 0; a < (int)gs.towns.size(); ++a)
        for (int b = a + 1; b < (int)gs.towns.size(); ++b) {
            const Vector2 ta = gs.towns[a].pos, tb = gs.towns[b].pos;
            if (Vector2Distance(ta, tb) >= m.roadLinkDist) continue;
            const Vector2 ab = Vector2Subtract(tb, ta);
            const float len2 = Vector2LengthSqr(ab);
            if (len2 <= 1.0f) continue;
            const float t = Clamp(Vector2DotProduct(Vector2Subtract(p, ta), ab) / len2,
                                  0.0f, 1.0f);
            const Vector2 close = Vector2Add(ta, Vector2Scale(ab, t));
            if (Vector2Distance(p, close) < m.roadWidth) return true;
        }
    return false;
}

// Travel pace at a point: forests and mountains slow every party (player and
// AI alike); a road negates the penalty. Factors live in the map (K8).
inline float TravelSpeedFactor(const GameState& gs, Vector2 p) {
    const MapDef& m = gs.content.map;
    const WorldTerrain t = WorldTerrainAt(m, p);
    if (t == WorldTerrain::Plains) return 1.0f;
    if (OnRoad(gs, p)) return 1.0f;
    return t == WorldTerrain::Forest ? m.biome.forestSpeed : m.biome.mountainSpeed;
}

// Relations (F1): deeds move the player's standing with a faction, clamped
// to -100..100. Marriage (M5) floors standing with the spouse's house at 0 —
// family forgives. Shared by campaign deeds and court dialogue.
inline void NudgeRelation(GameState& gs, int faction, int delta) {
    if (faction < 0 || faction >= gs.content.factions.size() ||
        faction == gs.content.playerFaction) return;
    if ((int)gs.relations.size() < gs.content.factions.size())
        gs.relations.assign(gs.content.factions.size(), 0);
    int& r = gs.relations[faction];
    r += delta;
    if (r > 100) r = 100;
    if (r < -100) r = -100;
    if (faction == gs.spouseFaction && r < 0) r = 0;
}

// Relief battles (P2): the defending crown's lord close enough to a
// settlement joins its garrison when the player assaults. The world is
// frozen during a battle, so calling this at launch (to merge his host into
// the setup) and again at the aftermath (to settle his fate) agrees.
inline constexpr float RELIEF_REACH = 250.0f;   // TODO(balance)

inline int ReliefLordFor(const GameState& gs, int town) {
    if (town < 0 || town >= (int)gs.towns.size()) return -1;
    const Town& t = gs.towns[town];
    // Villages fall (or burn) before help can arrive — relief armies only
    // reach fights that take time: walled sieges.
    if (t.type == SettlementType::Village) return -1;
    for (int i = 0; i < (int)gs.parties.size(); ++i) {
        const Party& p = gs.parties[i];
        if (!p.alive || p.engaged || p.lord.empty()) continue;
        if (p.faction != t.owner) continue;
        if (Vector2Distance(p.pos, t.pos) <= RELIEF_REACH) return i;
    }
    return -1;
}

// A named lord's stored opinion of the player (N4), created at 0 on first
// touch. Read through EffectiveLordOpinion, which adds the player's honor.
inline int& LordOpinion(GameState& gs, const std::string& name) {
    for (auto& p : gs.lordOpinion)
        if (p.first == name) return p.second;
    gs.lordOpinion.push_back({ name, 0 });
    return gs.lordOpinion.back().second;
}

inline int EffectiveLordOpinion(GameState& gs, const std::string& name) {
    return LordOpinion(gs, name) + gs.honor;
}

// Whose court is the player standing in (S4)? A map parley speaks for the
// lord's own faction; a settlement audience for the town's owner. -1 = none.
inline int AudienceFaction(const GameState& gs) {
    if (gs.parleyParty >= 0 && gs.parleyParty < (int)gs.parties.size())
        return gs.parties[gs.parleyParty].faction;
    if (gs.currentSettlement >= 0 && gs.currentSettlement < (int)gs.towns.size())
        return gs.towns[gs.currentSettlement].owner;
    return -1;
}

// A hero attribute by index (V14): 0 str, 1 agi, 2 int, 3 cha — the
// registry order in content.cpp.
inline int HeroAttr(const GameState& gs, int i) {
    return i >= 0 && i < (int)gs.playerHero.attributes.size()
               ? gs.playerHero.attributes[i] : 0;
}

// Party-size cap (M1): a base band plus one man per point of renown — and
// one per point of Charisma (V14: the attributes awaken). TODO(balance).
inline int PartyCap(const GameState& gs) {
    return 20 + gs.renown + HeroAttr(gs, 3);
}

// Renown a crown demands before it accepts an oath (M1). TODO(balance).
inline constexpr int RENOWN_TO_SWEAR = 5;

// The gear a hired companion fights in (K6): their fitted loadout if the
// player has dressed them, created from the catalogue default on first use.
inline Loadout& CompanionGear(GameState& gs, int troop) {
    for (auto& p : gs.companionGear)
        if (p.first == troop) return p.second;
    gs.companionGear.push_back({ troop, gs.content.troops[troop].loadout });
    return gs.companionGear.back().second;
}

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
// War economy (V31): a town whose owner fights wars pays war prices — each
// front adds 12% to everything on the shelf, capped at three fronts. Called
// at world init, at every dawn, and after loading a save, so diplomacy
// (declarations, truces, conquest) reaches the shelves by next morning.
inline void RefreshWarMarkups(GameState& gs);

inline bool AtWar(const GameState& gs, int a, int b) {
    const int n = gs.content.factions.size();
    if (a < 0 || b < 0 || a == b) return AreFactionsHostile(gs.content, a, b);
    if ((int)gs.hostile.size() == n * n) return gs.hostile[(size_t)a * n + b] != 0;
    return AreFactionsHostile(gs.content, a, b);
}

inline void RefreshWarMarkups(GameState& gs) {
    const int n = gs.content.factions.size();
    for (Town& t : gs.towns) {
        int fronts = 0;
        for (int f = 0; f < n && fronts < 3; ++f)   // kingdom wars only —
            if (f != t.owner && gs.content.factions[f].kingdom &&   // outlaw
                AtWar(gs, t.owner, f)) fronts++;    // raiding is the weather,
                                                    // not a war economy

        t.warMarkup = 100 + 12 * fronts;   // TODO(balance)
    }
}
