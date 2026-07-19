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
    int         upgradesTo = -1;   // troop handle this unit can become (-1 none)
    int         upgradeXp  = 0;    // TODO(balance): experience one upgrade costs
    Loadout     loadout;           // default gear (drives stats + look)
    Color       accent = WHITE;    // small identifying plume/banner colour
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
};

// The whole game catalogue. Everything is added in LoadDefaultContent(); to add
// content you register more defs there — no other code needs to change.
struct Content {
    Registry<ArmorDef>   armor;
    Registry<WeaponDef>  weapons;
    Registry<TroopDef>   troops;
    Registry<FactionDef> factions;

    int playerFaction = -1;  // resolved after loading

    // Symmetric factions×factions hostility matrix (1 = at war), filled at the
    // end of LoadDefaultContent(). Query via AreFactionsHostile().
    std::vector<unsigned char> hostile;
};

// Registers the base (unbalanced) content set. Single source of truth for all
// armour, weapons, troops and factions.
void LoadDefaultContent(Content& c);

// Total armour value of everything worn in `lo` (sums ArmorDef::armor).
int LoadoutArmor(const Content& c, const Loadout& lo);

// Whether two factions will fight. Currently any two *different* factions are
// hostile and the same faction is friendly — enough for a chaotic warband world
// where everyone raids everyone. A per-faction relations table can replace this
// later without changing any caller.
bool AreFactionsHostile(const Content& c, int factionA, int factionB);
