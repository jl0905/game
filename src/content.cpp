#include "content.h"

// ---------------------------------------------------------------------------
// The base content set. This is the ONE place to add armour, weapons, troops
// and factions. Adding an entry here makes it available everywhere via its
// handle — no other code changes required.
//
// The values below are intentionally FLAT placeholders, not a tuned game. A
// later balancing pass replaces `base::*` with real per-def numbers. Keeping
// everything flat now means appearance/behaviour can be built and reviewed
// without baking in balance decisions.
// ---------------------------------------------------------------------------

namespace base {
// Shared placeholder constants so no single def encodes a balance choice.
constexpr int   TROOP_HP      = 100;
constexpr float TROOP_SPEED   = 6.0f;
constexpr int   TROOP_COST    = 0;
constexpr float WEAPON_DAMAGE = 10.0f;
constexpr float WEAPON_REACH  = 2.5f;
constexpr float WEAPON_SWING  = 0.7f;
constexpr int   ARMOR_VALUE   = 0;
constexpr float ARMOR_WEIGHT  = 0.0f;
}  // namespace base

// Small helpers to keep the registration list readable.
static ArmorDef Armor(const char* id, const char* name, EquipSlot slot, Color tint) {
    return ArmorDef{ id, name, slot, base::ARMOR_VALUE, base::ARMOR_WEIGHT, tint };
}
static WeaponDef Weapon(const char* id, const char* name, WeaponClass wc, Color tint) {
    return WeaponDef{ id, name, wc, base::WEAPON_REACH, base::WEAPON_DAMAGE,
                      base::WEAPON_SWING, tint };
}

void LoadDefaultContent(Content& c) {
    // ---- Armour (grouped by slot) -----------------------------------------
    // Add new coverage by registering more entries with the matching slot.
    const int a_cap    = c.armor.add(Armor("cap",    "Cloth Cap",    EquipSlot::Head, BROWN));
    const int a_helmet = c.armor.add(Armor("helmet", "Iron Helmet",  EquipSlot::Head, GRAY));
    const int a_tunic  = c.armor.add(Armor("tunic",  "Padded Tunic", EquipSlot::Body, BEIGE));
    const int a_mail   = c.armor.add(Armor("mail",   "Mail Hauberk", EquipSlot::Body, DARKGRAY));
    const int a_plate  = c.armor.add(Armor("plate",  "Plate Cuirass",EquipSlot::Body, LIGHTGRAY));
    const int a_gloves = c.armor.add(Armor("gloves", "Leather Gloves",EquipSlot::Hands, DARKBROWN));
    const int a_boots  = c.armor.add(Armor("boots",  "Leather Boots",EquipSlot::Feet, DARKBROWN));

    // ---- Weapons (one per class shown; add freely) ------------------------
    const int w_sword  = c.weapons.add(Weapon("sword",  "Arming Sword", WeaponClass::OneHanded, LIGHTGRAY));
    const int w_great  = c.weapons.add(Weapon("great",  "Greatsword",   WeaponClass::TwoHanded, RAYWHITE));
    const int w_spear  = c.weapons.add(Weapon("spear",  "Spear",        WeaponClass::Polearm,   BROWN));
    const int w_bow    = c.weapons.add(Weapon("bow",    "Short Bow",    WeaponClass::Ranged,    DARKBROWN));
    (void)w_great; (void)w_bow;  // registered and ready; not yet issued to a troop

    // ---- Troops ------------------------------------------------------------
    // Troops differ by loadout + identity, NOT by tuned numbers (all flat base).
    auto makeTroop = [&](const char* id, const char* name, Color accent) {
        TroopDef t;
        t.id = id; t.name = name;
        t.maxHp = base::TROOP_HP; t.moveSpeed = base::TROOP_SPEED; t.cost = base::TROOP_COST;
        t.accent = accent;
        return t;
    };

    TroopDef recruit = makeTroop("recruit", "Recruit", LIGHTGRAY);
    recruit.loadout.set(EquipSlot::Body,   a_tunic);
    recruit.loadout.set(EquipSlot::Feet,   a_boots);
    recruit.loadout.set(EquipSlot::Weapon, w_sword);

    TroopDef infantry = makeTroop("infantry", "Infantry", SKYBLUE);
    infantry.loadout.set(EquipSlot::Head,   a_cap);
    infantry.loadout.set(EquipSlot::Body,   a_mail);
    infantry.loadout.set(EquipSlot::Hands,  a_gloves);
    infantry.loadout.set(EquipSlot::Feet,   a_boots);
    infantry.loadout.set(EquipSlot::Weapon, w_spear);

    TroopDef veteran = makeTroop("veteran", "Veteran", GOLD);
    veteran.loadout.set(EquipSlot::Head,   a_helmet);
    veteran.loadout.set(EquipSlot::Body,   a_plate);
    veteran.loadout.set(EquipSlot::Hands,  a_gloves);
    veteran.loadout.set(EquipSlot::Feet,   a_boots);
    veteran.loadout.set(EquipSlot::Weapon, w_sword);

    const int t_recruit  = c.troops.add(recruit);
    const int t_infantry = c.troops.add(infantry);
    const int t_veteran  = c.troops.add(veteran);

    // ---- Factions ----------------------------------------------------------
    // Distinct behaviours + rosters give the map its variety of party types.
    FactionDef kingdom;
    kingdom.id = "kingdom"; kingdom.name = "Your Warband";
    kingdom.color = BLUE; kingdom.behavior = PartyBehavior::Patrol;
    kingdom.recruitable = true;
    kingdom.roster = { t_recruit, t_infantry, t_veteran };
    c.playerFaction = c.factions.add(kingdom);

    FactionDef raiders;
    raiders.id = "raiders"; raiders.name = "Raiders";
    raiders.color = RED; raiders.behavior = PartyBehavior::Aggressive;
    raiders.roster = { t_recruit, t_infantry };
    c.factions.add(raiders);

    FactionDef deserters;
    deserters.id = "deserters"; deserters.name = "Deserters";
    deserters.color = ORANGE; deserters.behavior = PartyBehavior::Passive;
    deserters.roster = { t_recruit };
    c.factions.add(deserters);

    FactionDef patrol;
    patrol.id = "patrol"; patrol.name = "Patrol";
    patrol.color = PURPLE; patrol.behavior = PartyBehavior::Patrol;
    patrol.roster = { t_infantry, t_veteran };
    c.factions.add(patrol);
}
