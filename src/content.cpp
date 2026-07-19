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
    WeaponDef w;
    w.id = id; w.name = name; w.wclass = wc;
    w.reach = base::WEAPON_REACH;
    w.damage = base::WEAPON_DAMAGE;
    w.swingTime = base::WEAPON_SWING;
    w.tint = tint;
    return w;
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
    // Reach differs by weapon *identity* (a spear is simply longer than a
    // sword); damage/swing stay flat. TODO(balance): tune all three per def.
    WeaponDef sword = Weapon("sword", "Arming Sword", WeaponClass::OneHanded, LIGHTGRAY);
    WeaponDef great = Weapon("great", "Greatsword",   WeaponClass::TwoHanded, RAYWHITE);
    great.reach = 3.2f;
    WeaponDef spear = Weapon("spear", "Spear",        WeaponClass::Polearm,   BROWN);
    spear.reach = 4.0f;
    WeaponDef bow   = Weapon("bow",   "Short Bow",    WeaponClass::Ranged,    DARKBROWN);
    bow.missileRange = 40.0f;   // TODO(balance)
    bow.missileSpeed = 30.0f;   // TODO(balance)
    bow.swingTime    = 2.0f;    // TODO(balance): nock-draw-loose is slower than a cut

    const int w_sword = c.weapons.add(sword);
    const int w_great = c.weapons.add(great);
    const int w_spear = c.weapons.add(spear);
    const int w_bow   = c.weapons.add(bow);

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
    infantry.loadout.addWeapon(w_spear);   // carries both — spear at range,
    infantry.loadout.addWeapon(w_sword);   // sword up close (AI picks per range)

    TroopDef veteran = makeTroop("veteran", "Veteran", GOLD);
    veteran.loadout.set(EquipSlot::Head,   a_helmet);
    veteran.loadout.set(EquipSlot::Body,   a_plate);
    veteran.loadout.set(EquipSlot::Hands,  a_gloves);
    veteran.loadout.set(EquipSlot::Feet,   a_boots);
    veteran.loadout.addWeapon(w_great);    // a veteran carries a greatsword
    veteran.loadout.addWeapon(w_sword);    // and a sidearm

    TroopDef archer = makeTroop("archer", "Archer", GREEN);
    archer.loadout.set(EquipSlot::Head,   a_cap);
    archer.loadout.set(EquipSlot::Body,   a_tunic);
    archer.loadout.set(EquipSlot::Feet,   a_boots);
    archer.loadout.addWeapon(w_bow);       // shoots at range,
    archer.loadout.addWeapon(w_sword);     // draws steel when cornered

    const int t_recruit  = c.troops.add(recruit);
    const int t_infantry = c.troops.add(infantry);
    const int t_veteran  = c.troops.add(veteran);
    const int t_archer   = c.troops.add(archer);

    // ---- Factions ----------------------------------------------------------
    // Distinct behaviours + rosters give the map its variety of party types.
    FactionDef kingdom;
    kingdom.id = "kingdom"; kingdom.name = "Your Warband";
    kingdom.color = BLUE; kingdom.behavior = PartyBehavior::Patrol;
    kingdom.recruitable = true;
    kingdom.roster = { t_recruit, t_infantry, t_veteran, t_archer };
    c.playerFaction = c.factions.add(kingdom);

    FactionDef raiders;
    raiders.id = "raiders"; raiders.name = "Raiders";
    raiders.color = RED; raiders.behavior = PartyBehavior::Aggressive;
    raiders.roster = { t_recruit, t_infantry, t_archer };
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
    const int f_patrol = c.factions.add(patrol);

    // ---- Relations -------------------------------------------------------
    // Who is at war with whom. Outlaws (raiders, deserters) are hostile to
    // everyone including each other; the lawful factions (your warband and
    // the patrols) keep the peace with one another.
    const int n = c.factions.size();
    c.hostile.assign((size_t)n * n, 0);
    auto war = [&](int a, int b) {
        c.hostile[(size_t)a * n + b] = c.hostile[(size_t)b * n + a] = 1;
    };
    const int f_raiders   = c.factions.find("raiders");
    const int f_deserters = c.factions.find("deserters");
    for (int f = 0; f < n; ++f) {
        if (f != f_raiders)   war(f_raiders, f);
        if (f != f_deserters) war(f_deserters, f);
    }
    (void)f_patrol;   // at peace with the player by omission
}

int LoadoutArmor(const Content& c, const Loadout& lo) {
    int total = 0;
    for (int s = 0; s < EQUIP_SLOT_COUNT; ++s) {
        if (s == static_cast<int>(EquipSlot::Weapon)) continue;
        const int h = lo.slots[s];
        if (c.armor.valid(h)) total += c.armor[h].armor;
    }
    return total;
}

bool AreFactionsHostile(const Content& c, int factionA, int factionB) {
    const int n = c.factions.size();
    if (factionA < 0 || factionB < 0 || factionA >= n || factionB >= n) return false;
    if ((int)c.hostile.size() != n * n)   // no relations table loaded: all-out war
        return factionA != factionB;
    return c.hostile[(size_t)factionA * n + factionB] != 0;
}
