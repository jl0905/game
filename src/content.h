#pragma once
#include "types.h"
#include "registry.h"
#include <array>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Content definitions: the data-driven catalogue of everything in the game.
//
// IMPORTANT — NO BALANCE VALUES LIVE HERE BY DESIGN.
// Every numeric field below is a PLACEHOLDER. The game needs *some* numbers to
// run, so all defs use the deliberately-flat constants in `base` (see
// content.cpp). Balancing is a later, human, playtesting-driven pass: tune the
// per-def values in content.cpp, not the structure here.
// ---------------------------------------------------------------------------

// A piece of wearable armour occupying one equipment slot.
struct ArmorDef {
    std::string id;
    std::string name;
    EquipSlot   slot   = EquipSlot::Body;
    int         armor  = 0;     // TODO(balance): damage reduction
    float       weight = 0.0f;  // TODO(balance): encumbrance / speed penalty
    int         tileW  = 2;     // inventory footprint (identity, not balance)
    int         tileH  = 2;
    Color       tint   = GRAY;  // rendering only
};

// A weapon occupying the Weapon slot.
struct WeaponDef {
    std::string id;
    std::string name;
    WeaponClass wclass    = WeaponClass::OneHanded;
    float       reach     = 0.0f;  // TODO(balance): attack range
    float       damage    = 0.0f;  // TODO(balance): hit damage
    float       swingTime = 0.0f;  // TODO(balance): cooldown between swings
    // Ranged weapons only (wclass == WeaponClass::Ranged):
    float       missileRange = 0.0f;  // TODO(balance): how far it can shoot
    float       missileSpeed = 0.0f;  // TODO(balance): projectile speed
    int         tileW = 1;            // inventory footprint (identity, not balance)
    int         tileH = 3;
    Color       tint      = LIGHTGRAY;

    bool isRanged() const { return wclass == WeaponClass::Ranged && missileRange > 0.5f; }
};

// What a character wears and wields. Each slot holds a handle into the matching
// registry (armor for wearables, weapons for the Weapon slot); -1 means empty.
//
// A character may carry MULTIPLE weapons (an arsenal), not just one. The Weapon
// slot always mirrors the currently-active weapon so rendering and combat can
// read a single handle; `weapons` holds the full set to switch between. Add
// weapons with addWeapon() — the first one becomes active automatically.
struct Loadout {
    std::array<int, EQUIP_SLOT_COUNT> slots;
    std::vector<int>                  weapons;   // carried arsenal (handles)
    Loadout() { slots.fill(-1); }

    int  get(EquipSlot s) const { return slots[static_cast<int>(s)]; }
    void set(EquipSlot s, int handle) { slots[static_cast<int>(s)] = handle; }
    bool has(EquipSlot s) const { return get(s) >= 0; }

    // Add a weapon to the arsenal; the first added also becomes the active one.
    void addWeapon(int handle) {
        weapons.push_back(handle);
        if (!has(EquipSlot::Weapon)) set(EquipSlot::Weapon, handle);
    }

    // Arsenal size, tolerating loadouts that only set the Weapon slot directly.
    int weaponCount() const {
        return weapons.empty() ? (has(EquipSlot::Weapon) ? 1 : 0)
                               : static_cast<int>(weapons.size());
    }

    // Handle of the i-th carried weapon (falls back to the Weapon slot).
    int weaponAt(int i) const {
        if (i >= 0 && i < static_cast<int>(weapons.size())) return weapons[i];
        return get(EquipSlot::Weapon);
    }
};

// A troop archetype: a named unit with default equipment.
struct TroopDef {
    std::string id;
    std::string name;
    int         maxHp     = 0;     // TODO(balance)
    float       moveSpeed = 0.0f;  // TODO(balance)
    int         cost      = 0;     // TODO(balance): recruit cost in gold
    int         wage      = 0;     // TODO(balance): per-day upkeep in gold
    int         upgradesTo = -1;   // troop handle this unit can become (-1 none)
    int         upgradeXp  = 0;    // TODO(balance): experience one upgrade costs
    bool        mounted   = false; // rides a horse: drawn mounted, tramples on the charge
    bool        companion = false; // a unique named hero-for-hire (H1): at most one
                                   // in the party, hired in taverns, in no roster
    std::string temper;            // companion voice (P3): "honorable" objects to
                                   // black deeds and may leave; "grim" approves
    std::string perk;              // companion party bonus (V54): "surgeon" saves
                                   // more of the fallen, "scout" hastens the march,
                                   // "quartermaster" stretches the grain. Data-only:
                                   // new perks are new strings + one effect site.
    Loadout     loadout;           // default gear (drives stats + look)
    Color       accent = WHITE;    // small identifying plume/banner colour
};

// A tradable good (direction E1). Goods are stackable quantities — unlike
// armour/weapons they never occupy inventory tiles; the player's holdings and
// each settlement's stock are simple counts parallel to this registry.
struct GoodDef {
    std::string id;
    std::string name;
    int         basePrice = 0;   // TODO(balance): gold per unit before offsets
    bool        raw = true;      // village produce (cheap there) vs town craftwork
    Color       tint      = BEIGE;  // market-row swatch
};

// A productive enterprise the player can buy in a town (direction E4):
// pays daily income scaled by the town's prosperity, lost if the town falls
// to an enemy of the player.
struct EnterpriseDef {
    std::string id;
    std::string name;
    int cost        = 0;   // TODO(balance): purchase price
    int dailyIncome = 0;   // TODO(balance): base gold per day
    // What the works turns out (V8): owning it makes this good local
    // produce — priced at source, produced daily by the living market.
    std::string makesGood;
};

// A quest shape (direction F4), offered by settlement givers. `amount` is the
// count the type needs (parties to hunt, goods to carry).
struct QuestDef {
    std::string id;
    std::string name;
    std::string blurb;         // one line of giver flavour
    QuestType   type = QuestType::HuntBandits;
    int         amount = 1;    // TODO(balance)
    int         goldReward = 0;      // TODO(balance)
    int         relationReward = 0;  // TODO(balance)
    std::string goodId;        // Deliver quests carry ANY registered ware
                               // (V18) — quests join the goods registry
    int         days = 0;      // deadline in days (V59); 0 = no clock.
                               // TODO(balance)
};

// A world event (R4): fires at a settlement on the day rotation and is
// announced as news. Registered like any other content — modders add events
// the way they add goods. All numbers flat TODO(balance).
struct EventDef {
    std::string id;
    std::string name;
    std::string news;             // "%s" is the struck settlement's name
    int prosperityDelta = 0;      // applied to the settlement, clamped 30..150
    int stockDelta      = 0;      // per good, floored at 0
    int spawnParties    = 0;      // outlaw bands raised at the site
    int poolDelta       = 0;      // recruit-pool swing (V4): events touch
                                  // manpower too
};

// An estate/town building (V135, the kingdom-management track). One registry
// serves BOTH the player's personal estate and (later) town projects — the
// Skyrim-homestead / personal-castle layer, kept rooted in the battle-sim
// core: every effect hooks an EXISTING system by id, no new parallel logic.
//   effect ids wired so far: "fields"  (daily gold by linked prosperity),
//   "barracks" (+party cap, +recruit pool), "smithy" (promotions cheaper),
//   "granary" (hunger never bites while it stands), "walls" (estate battles
//   fight fortified). New effects = new string + one hook site.
struct BuildingDef {
    std::string id;
    std::string name;
    std::string desc;      // one line shown in the estate hall
    int         cost = 0;  // TODO(balance): gold to start construction
    int         days = 0;  // TODO(balance): dawns until it stands
    std::string effect;    // hook id (see above)
};

// A hero attribute (roadmap D3). Pure structure: `hook` documents what the
// attribute WILL modify once balancing begins; nothing reads values yet.
struct AttributeDef {
    std::string id;
    std::string name;
    std::string hook;   // human-readable list of intended effects
};

// A faction fields particular troops and roams with a particular behaviour.
// This is how "different types of parties" exist on the campaign map.
struct FactionDef {
    std::string      id;
    std::string      name;
    Color            color    = RED;
    PartyBehavior    behavior = PartyBehavior::Patrol;
    std::vector<int> roster;        // troop handles this faction can field
    bool             recruitable = false;  // can the player hire from towns?

    // Named lords this faction fields as large armies (roadmap C3). One-token
    // names (drawn as "Lord <name>"). Empty = the faction has no lords.
    std::vector<std::string> lords;
    int lordPartySize = 0;          // TODO(balance): order-of-hundreds army size

    // Kingdoms wage declared wars and can swear truces (live diplomacy, C4);
    // outlaw rabble fights everyone forever and never treats.
    bool kingdom = false;

    // Sellswords (V29): lords of a mercenary faction offer their company for
    // hire in audience — a contract shadows your march and joins your battles.
    bool mercenary = false;
};

// The overworld map (direction I1): bounds, the player's start and every
// settlement. LoadDefaultContent installs a built-in default; LoadMapConfig
// then overlays assets/map.cfg when present, so modders reshape the world —
// size, settlements, owners — without a rebuild.
struct MapDef {
    struct TownSpec {
        Vector2        pos{};
        std::string    name;                        // one token (map label)
        SettlementType type = SettlementType::Town;
        std::string    owner;                       // faction id, "player" or "none"
    };
    // A bandit den (H2): keeps spawning parties of its faction until raided.
    struct LairSpec {
        Vector2     pos{};
        std::string faction;
    };
    // Biome field (K8): one noise formula paints the map, classifies terrain
    // for travel speed, and seeds battlefields — modded here, felt everywhere.
    struct BiomeSpec {
        float hillFreqX   = 0.0031f, hillFreqY   = 0.0027f;
        float forestFreqX = 0.0012f, forestFreqY = 0.0019f;
        float forestThreshold   = 0.35f;   // n2 above this = forest
        float mountainThreshold = 0.55f;   // n1 above this = mountain
        float forestSpeed   = 0.7f;        // TODO(balance): march factors
        float mountainSpeed = 0.55f;
    };
    float                 size = 2000.0f;
    Vector2               playerStart{ 1000.0f, 1000.0f };
    int                   startingParties = 5;      // roaming parties at world start
    std::vector<TownSpec> towns;
    std::vector<LairSpec> lairs;
    BiomeSpec             biome;
    float                 roadLinkDist = 1350.0f;   // towns closer than this are joined
    float                 roadWidth    = 20.0f;     // within this of a link = on the road
    // The storm (V62/V68): moddable weather. Radius of the cell, its
    // per-dawn drift, and where it rolls in from at world start.
    float   stormRadius = 250.0f;                   // TODO(balance)
    Vector2 stormDrift  = { 90.0f, -60.0f };
    Vector2 stormStart  = { -1.0f, -1.0f };         // <0 = fraction defaults
    std::vector<std::string> lordNames =            // pool for player-raised lords (F3)
        { "Bram", "Edric", "Sable", "Corwin" };
};

// The whole game catalogue. Everything is added in LoadDefaultContent(); to add
// content you register more defs there — no other code needs to change.
struct Content {
    Registry<ArmorDef>     armor;
    Registry<WeaponDef>    weapons;
    Registry<TroopDef>     troops;
    Registry<FactionDef>   factions;
    Registry<AttributeDef> attributes;
    Registry<GoodDef>      goods;
    Registry<EnterpriseDef> enterprises;
    Registry<QuestDef>     quests;
    Registry<EventDef>     events;
    Registry<BuildingDef>  buildings;   // estate & town works (V135)
    MapDef                 map;

    int playerFaction = -1;  // resolved after loading

    // Symmetric factions×factions hostility matrix (1 = at war), filled at the
    // end of LoadDefaultContent(). Query via AreFactionsHostile().
    std::vector<unsigned char> hostile;
};

// Registers the base (unbalanced) content set. Single source of truth for all
// armour, weapons, troops and factions.
void LoadDefaultContent(Content& c);

// Overlay the moddable overworld map from a cfg file onto Content::map.
// Missing file or field keeps the built-in default; called by
// LoadDefaultContent with the standard assets path.
void LoadMapConfig(Content& c, const char* path);

// Total armour value of everything worn in `lo` (sums ArmorDef::armor).
int LoadoutArmor(const Content& c, const Loadout& lo);

// ---- the gear economy (V134) ----------------------------------------------
// Prices are CODIFIED, not tabled: every item's cost derives from its stats
// through one formula, so adding a stronger sword automatically prices it.
// Troop promotion charges exactly the gear delta between the two tiers plus
// the stat training — the same gold a player pays to buy those pieces
// outright at a market armoury. TODO(balance): every coefficient.
int WeaponCost(const WeaponDef& w);
int ArmorCost(const ArmorDef& a);
int LoadoutCost(const Content& c, const Loadout& lo);   // worn + carried
int UpgradeCost(const Content& c, int fromTroop, int toTroop);

// Whether two factions will fight. Currently any two *different* factions are
// hostile and the same faction is friendly — enough for a chaotic warband world
// where everyone raids everyone. A per-faction relations table can replace this
// later without changing any caller.
bool AreFactionsHostile(const Content& c, int factionA, int factionB);
