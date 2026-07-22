#include "content.h"
#include <fstream>
#include <sstream>

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
constexpr int   UPGRADE_XP    = 100;   // experience one troop upgrade costs
constexpr int   TROOP_WAGE    = 1;     // per-day upkeep per soldier
}  // namespace base

// Small helpers to keep the registration list readable.
static ArmorDef Armor(const char* id, const char* name, EquipSlot slot, Color tint) {
    ArmorDef a;
    a.id = id; a.name = name; a.slot = slot;
    a.armor = base::ARMOR_VALUE;
    a.weight = base::ARMOR_WEIGHT;
    a.tint = tint;
    return a;
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
    // Armour awakens (V15): identity-tier soak values — cloth 1, iron 3,
    // mail 4, plate 6 — flowing through the LoadoutArmor -> ApplyArmor
    // pipeline that has waited for them since the combat model shipped.
    // Tiers are identity; exact numbers still TODO(balance).
    ArmorDef cap    = Armor("cap",    "Cloth Cap",    EquipSlot::Head, BROWN);
    ArmorDef helmet = Armor("helmet", "Iron Helmet",  EquipSlot::Head, GRAY);
    ArmorDef tunic  = Armor("tunic",  "Padded Tunic", EquipSlot::Body, BEIGE);
    ArmorDef mail   = Armor("mail",   "Mail Hauberk", EquipSlot::Body, DARKGRAY);
    ArmorDef plate  = Armor("plate",  "Plate Cuirass",EquipSlot::Body, LIGHTGRAY);
    ArmorDef kettle = Armor("kettle", "Kettle Helm",  EquipSlot::Head, Color{ 105, 105, 115, 255 });
    ArmorDef fur    = Armor("fur",    "Fur Cloak",    EquipSlot::Body, Color{ 88, 66, 48, 255 });
    ArmorDef gloves = Armor("gloves", "Leather Gloves",EquipSlot::Hands, DARKBROWN);
    ArmorDef boots  = Armor("boots",  "Leather Boots",EquipSlot::Feet, DARKBROWN);
    cap.armor = 1;  helmet.armor = 3; tunic.armor = 1;  mail.armor = 4;
    plate.armor = 6; kettle.armor = 3; fur.armor = 1;   gloves.armor = 1;
    boots.armor = 1;
    const int a_cap    = c.armor.add(cap);
    const int a_helmet = c.armor.add(helmet);
    const int a_tunic  = c.armor.add(tunic);
    const int a_mail   = c.armor.add(mail);
    const int a_plate  = c.armor.add(plate);
    const int a_kettle = c.armor.add(kettle);
    const int a_fur    = c.armor.add(fur);
    const int a_gloves = c.armor.add(gloves);
    const int a_boots  = c.armor.add(boots);

    // ---- Weapons (one per class shown; add freely) ------------------------
    // Reach differs by weapon *identity* (a spear is simply longer than a
    // sword); damage/swing stay flat. TODO(balance): tune all three per def.
    // Weapon identity damage (V16, the mirror of V15's armour): a blade's
    // weight is its character now that steel soaks — the greatsword and
    // dane axe crack plate, the spear trades power for its four units of
    // reach, bows harass where crossbows punch. Tiers are identity; exact
    // numbers TODO(balance).
    WeaponDef sword = Weapon("sword", "Arming Sword", WeaponClass::OneHanded, LIGHTGRAY);
    WeaponDef great = Weapon("great", "Greatsword",   WeaponClass::TwoHanded, RAYWHITE);
    great.reach = 3.2f;
    great.damage = 14.0f;
    WeaponDef spear = Weapon("spear", "Spear",        WeaponClass::Polearm,   BROWN);
    spear.reach = 4.0f;
    spear.damage = 9.0f;
    WeaponDef bow   = Weapon("bow",   "Short Bow",    WeaponClass::Ranged,    DARKBROWN);
    bow.damage = 8.0f;
    bow.missileRange = 40.0f;   // TODO(balance)
    bow.missileSpeed = 30.0f;   // TODO(balance)
    bow.swingTime    = 2.0f;    // TODO(balance): nock-draw-loose is slower than a cut

    // Inventory footprints — big weapons take big bags (identity, not balance).
    great.tileW = 2; great.tileH = 4;
    spear.tileW = 1; spear.tileH = 4;
    bow.tileW   = 2; bow.tileH   = 3;

    // Extensibility proof (V1): a whole new ranged identity in four lines —
    // further, flatter, slower than the bow. No code elsewhere changes.
    WeaponDef xbow = Weapon("xbow", "Crossbow", WeaponClass::Ranged, DARKGRAY);
    xbow.missileRange = 55.0f;   // TODO(balance): outranges the bow
    xbow.missileSpeed = 45.0f;   // flat, fast bolts
    xbow.swingTime    = 3.2f;    // spanning a crossbow takes time
    xbow.damage       = 12.0f;   // and the bolt punches mail (V16)
    xbow.tileW = 2; xbow.tileH = 3;

    WeaponDef axe = Weapon("axe", "War Axe", WeaponClass::Axe, LIGHTGRAY);
    axe.tileW = 2; axe.tileH = 3;
    axe.damage = 11.0f;                 // heavier than a sword's cut (V16)

    WeaponDef daneaxe = Weapon("daneaxe", "Dane Axe", WeaponClass::Axe, RAYWHITE);
    daneaxe.reach = 3.2f;               // a long-hafted axe (identity, not balance)
    daneaxe.damage = 13.0f;             // and it cracks plate (V16)
    daneaxe.tileW = 2; daneaxe.tileH = 4;

    const int w_sword = c.weapons.add(sword);
    const int w_great = c.weapons.add(great);
    const int w_spear = c.weapons.add(spear);
    const int w_bow   = c.weapons.add(bow);
    const int w_axe   = c.weapons.add(axe);
    const int w_dane  = c.weapons.add(daneaxe);
    const int w_xbow  = c.weapons.add(xbow);

    // ---- Troops ------------------------------------------------------------
    // Troops differ by loadout + identity, NOT by tuned numbers (all flat base).
    auto makeTroop = [&](const char* id, const char* name, Color accent) {
        TroopDef t;
        t.id = id; t.name = name;
        t.maxHp = base::TROOP_HP; t.moveSpeed = base::TROOP_SPEED; t.cost = base::TROOP_COST;
        t.wage = base::TROOP_WAGE;
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

    TroopDef knight = makeTroop("knight", "Knight", ORANGE);
    knight.mounted   = true;
    knight.moveSpeed = base::TROOP_SPEED * 2.0f;   // a horse is simply faster (identity)
    knight.loadout.set(EquipSlot::Head,   a_helmet);
    knight.loadout.set(EquipSlot::Body,   a_plate);
    knight.loadout.set(EquipSlot::Hands,  a_gloves);
    knight.loadout.set(EquipSlot::Feet,   a_boots);
    knight.loadout.addWeapon(w_spear);     // couched at the charge,
    knight.loadout.addWeapon(w_sword);     // steel in the press

    TroopDef brigand = makeTroop("brigand", "Brigand", MAROON);
    brigand.loadout.set(EquipSlot::Head,   a_cap);
    brigand.loadout.set(EquipSlot::Body,   a_tunic);
    brigand.loadout.set(EquipSlot::Feet,   a_boots);
    brigand.loadout.addWeapon(w_axe);      // an outlaw's tool of trade

    TroopDef archer = makeTroop("archer", "Archer", GREEN);
    archer.loadout.set(EquipSlot::Head,   a_cap);
    archer.loadout.set(EquipSlot::Body,   a_tunic);
    archer.loadout.set(EquipSlot::Feet,   a_boots);
    archer.loadout.addWeapon(w_bow);       // shoots at range,
    archer.loadout.addWeapon(w_sword);     // draws steel when cornered

    // Third-tier specialists (H2): every line gets somewhere to season into.
    TroopDef marksman = makeTroop("marksman", "Marksman", DARKGREEN);
    marksman.loadout.set(EquipSlot::Head,   a_kettle);
    marksman.loadout.set(EquipSlot::Body,   a_mail);
    marksman.loadout.set(EquipSlot::Feet,   a_boots);
    marksman.loadout.addWeapon(w_bow);
    marksman.loadout.addWeapon(w_sword);

    TroopDef marauder = makeTroop("marauder", "Marauder", Color{ 150, 60, 40, 255 });
    marauder.loadout.set(EquipSlot::Head,   a_helmet);
    marauder.loadout.set(EquipSlot::Body,   a_mail);
    marauder.loadout.set(EquipSlot::Feet,   a_boots);
    marauder.loadout.addWeapon(w_dane);
    marauder.loadout.addWeapon(w_axe);

    // The sea-kings' foot: fur-clad warriors who season into huscarls.
    TroopDef warrior = makeTroop("warrior", "Vaeling Warrior", Color{ 120, 170, 190, 255 });
    warrior.loadout.set(EquipSlot::Head,   a_cap);
    warrior.loadout.set(EquipSlot::Body,   a_fur);
    warrior.loadout.set(EquipSlot::Feet,   a_boots);
    warrior.loadout.addWeapon(w_axe);
    warrior.loadout.addWeapon(w_sword);

    TroopDef huscarl = makeTroop("huscarl", "Huscarl", Color{ 210, 225, 235, 255 });
    huscarl.loadout.set(EquipSlot::Head,   a_kettle);
    huscarl.loadout.set(EquipSlot::Body,   a_mail);
    huscarl.loadout.set(EquipSlot::Hands,  a_gloves);
    huscarl.loadout.set(EquipSlot::Feet,   a_boots);
    huscarl.loadout.addWeapon(w_dane);     // the long axe leads,
    huscarl.loadout.addWeapon(w_sword);    // steel finishes

    // ---- Companions (direction H1) ----------------------------------------
    // Unique heroes-for-hire found in taverns: one of each, ever, and no
    // faction fields them. Personality lives in the blurb the tavern shows;
    // numbers stay flat like every other troop. TODO(balance): hire costs.
    auto makeCompanion = [&](const char* id, const char* name) {
        TroopDef t = makeTroop(id, name, GOLD);
        t.companion = true;
        t.cost = 100;   // TODO(balance): hire price
        t.wage = base::TROOP_WAGE * 3;   // TODO(balance): heroes eat well
        return t;
    };
    TroopDef rega = makeCompanion("rega", "Rega the Grim");
    rega.temper = "grim";          // spoils are spoils (P3)
    rega.loadout.set(EquipSlot::Head,   a_helmet);
    rega.loadout.set(EquipSlot::Body,   a_plate);
    rega.loadout.set(EquipSlot::Hands,  a_gloves);
    rega.loadout.set(EquipSlot::Feet,   a_boots);
    rega.loadout.addWeapon(w_great);
    rega.loadout.addWeapon(w_sword);

    TroopDef malin = makeCompanion("malin", "Malin Longeye");
    malin.temper = "honorable";    // wants no part of burned villages (P3)
    malin.loadout.set(EquipSlot::Head,   a_cap);
    malin.loadout.set(EquipSlot::Body,   a_mail);
    malin.loadout.set(EquipSlot::Feet,   a_boots);
    malin.loadout.addWeapon(w_bow);
    malin.loadout.addWeapon(w_sword);

    TroopDef torva = makeCompanion("torva", "Torva Ironhand");
    torva.temper = "honorable";    // an oath-keeper to the bone (P3)
    torva.mounted = true;
    torva.moveSpeed = base::TROOP_SPEED * 2.0f;
    torva.loadout.set(EquipSlot::Head,   a_kettle);
    torva.loadout.set(EquipSlot::Body,   a_mail);
    torva.loadout.set(EquipSlot::Hands,  a_gloves);
    torva.loadout.set(EquipSlot::Feet,   a_boots);
    torva.loadout.addWeapon(w_spear);
    torva.loadout.addWeapon(w_axe);

    c.troops.add(rega);
    c.troops.add(malin);
    c.troops.add(torva);

    // Wage identity (V46): quality eats gold daily — a knight's horse and
    // plate cost six recruits' bread. Tiers are identity; exact numbers
    // TODO(balance). (Companions already draw triple pay above.)
    infantry.wage = 2; archer.wage   = 2; marauder.wage = 2; warrior.wage = 2;
    veteran.wage  = 3; marksman.wage = 3;
    huscarl.wage  = 4; knight.wage   = 6;

    const int t_recruit  = c.troops.add(recruit);
    const int t_infantry = c.troops.add(infantry);
    const int t_veteran  = c.troops.add(veteran);
    const int t_archer   = c.troops.add(archer);
    const int t_knight   = c.troops.add(knight);
    const int t_brigand  = c.troops.add(brigand);
    const int t_marksman = c.troops.add(marksman);
    const int t_marauder = c.troops.add(marauder);

    // Extensibility proof (V1): a new troop from parts already on the shelf —
    // the arbalist, fielded by the lawful crowns, appended so no handle moves.
    TroopDef arbalist = makeTroop("arbalist", "Arbalist", DARKBLUE);
    arbalist.loadout.set(EquipSlot::Head, a_kettle);
    arbalist.loadout.set(EquipSlot::Body, a_mail);
    arbalist.loadout.set(EquipSlot::Feet, a_boots);
    arbalist.loadout.addWeapon(w_xbow);    // outranges bows, spans slowly
    arbalist.loadout.addWeapon(w_sword);
    arbalist.wage = 3;   // wage identity (V46)
    const int t_arbalist = c.troops.add(arbalist);
    const int t_warrior  = c.troops.add(warrior);
    const int t_huscarl  = c.troops.add(huscarl);

    // Extensibility proof (V28): a new weapon identity AND the troop that
    // carries it, appended so no handle moves. The pike is the longest haft
    // on any field — it outreaches even the couched spear — but it is slow
    // to reset and weak up close, so pikemen carry steel for the press.
    WeaponDef pike = Weapon("pike", "Pike", WeaponClass::Polearm,
                            Color{ 168, 148, 116, 255 });
    pike.reach     = 5.2f;    // identity: nothing on the field reaches further
    pike.damage    = 10.0f;   // TODO(balance)
    pike.swingTime = 1.6f;    // TODO(balance): a pike is slow to reset
    pike.tileW = 1; pike.tileH = 4;
    const int w_pike = c.weapons.add(pike);

    TroopDef pikeman = makeTroop("pikeman", "Free Pikeman",
                                 Color{ 212, 175, 55, 255 });
    pikeman.loadout.set(EquipSlot::Head, a_kettle);
    pikeman.loadout.set(EquipSlot::Body, a_mail);
    pikeman.loadout.set(EquipSlot::Feet, a_boots);
    pikeman.loadout.addWeapon(w_pike);     // the wall of points at range,
    pikeman.loadout.addWeapon(w_sword);    // steel when the press closes
    pikeman.wage = 2;   // wage identity (V46): sellswords bill modestly
    const int t_pikeman = c.troops.add(pikeman);

    // Upgrade tree: recruit -> infantry -> veteran; archers are a branch off
    // recruit. Costs are flat placeholders.
    c.troops[t_recruit].upgradesTo  = t_infantry;
    c.troops[t_recruit].upgradeXp   = base::UPGRADE_XP;
    c.troops[t_infantry].upgradesTo = t_veteran;
    c.troops[t_infantry].upgradeXp  = base::UPGRADE_XP;
    c.troops[t_veteran].upgradesTo  = t_knight;    // a veteran earns his horse
    c.troops[t_veteran].upgradeXp   = base::UPGRADE_XP;
    c.troops[t_warrior].upgradesTo  = t_huscarl;   // the Vaeling line
    c.troops[t_warrior].upgradeXp   = base::UPGRADE_XP;
    c.troops[t_archer].upgradesTo   = t_marksman;  // the shooting line (H2)
    c.troops[t_archer].upgradeXp    = base::UPGRADE_XP;
    c.troops[t_brigand].upgradesTo  = t_marauder;  // outlaws harden too
    c.troops[t_brigand].upgradeXp   = base::UPGRADE_XP;

    // ---- Factions ----------------------------------------------------------
    // Distinct behaviours + rosters give the map its variety of party types.
    FactionDef kingdom;
    kingdom.id = "kingdom"; kingdom.name = "Your Warband";
    kingdom.color = BLUE; kingdom.behavior = PartyBehavior::Patrol;
    kingdom.recruitable = true;
    kingdom.kingdom = true;
    kingdom.lordPartySize = 60;   // TODO(balance): a raised lord's host (F3)
    kingdom.roster = { t_recruit, t_infantry, t_veteran, t_archer, t_knight };
    c.playerFaction = c.factions.add(kingdom);

    FactionDef raiders;
    raiders.id = "raiders"; raiders.name = "Raiders";
    raiders.color = RED; raiders.behavior = PartyBehavior::Aggressive;
    raiders.roster = { t_brigand, t_marauder, t_infantry, t_archer };   // axes lead
    raiders.lords = { "Gorak", "Hesh" };
    raiders.lordPartySize = 150;    // TODO(balance)
    c.factions.add(raiders);

    FactionDef deserters;
    deserters.id = "deserters"; deserters.name = "Deserters";
    deserters.color = ORANGE; deserters.behavior = PartyBehavior::Passive;
    deserters.roster = { t_recruit };
    deserters.lords = { "Vex" };
    deserters.lordPartySize = 150;  // TODO(balance)
    c.factions.add(deserters);

    FactionDef patrol;
    patrol.id = "patrol"; patrol.name = "Patrol";
    patrol.color = PURPLE; patrol.behavior = PartyBehavior::Patrol;
    patrol.roster = { t_infantry, t_veteran, t_knight, t_arbalist };
    patrol.lords = { "Aldric", "Corin" };
    patrol.lordPartySize = 150;     // TODO(balance)
    patrol.kingdom = true;
    const int f_patrol = c.factions.add(patrol);

    // The rival crown: a lawful kingdom with a claim of its own. Hostile to
    // the player and to the outlaws; the patrols keep the peace with both
    // crowns.
    FactionDef sarleon;
    sarleon.id = "sarleon"; sarleon.name = "Sarleon";
    sarleon.color = Color{ 0, 150, 150, 255 };
    sarleon.behavior = PartyBehavior::Patrol;
    sarleon.roster = { t_infantry, t_veteran, t_archer, t_knight, t_arbalist };
    sarleon.lords = { "Aldemar", "Rowan" };
    sarleon.lordPartySize = 150;    // TODO(balance)
    sarleon.kingdom = true;
    const int f_sarleon = c.factions.add(sarleon);

    // The sea-kings: a third crown from the cold coast, axe-heavy foot.
    FactionDef vaeling;
    vaeling.id = "vaeling"; vaeling.name = "Vaelings";
    vaeling.color = Color{ 70, 130, 60, 255 };
    vaeling.behavior = PartyBehavior::Patrol;
    vaeling.roster = { t_warrior, t_huscarl, t_archer };
    vaeling.lords = { "Sigvald", "Toke" };
    vaeling.lordPartySize = 150;    // TODO(balance)
    vaeling.kingdom = true;
    const int f_vaeling = c.factions.add(vaeling);

    // The small folk (M6): traders and pilgrims walking the roads between
    // settlements. Passive (they flee trouble), at peace with every crown;
    // the blanket outlaw wars below make them bandit prey automatically.
    FactionDef travellers;
    travellers.id = "travellers"; travellers.name = "Travellers";
    travellers.color = Color{ 200, 190, 150, 255 };
    travellers.behavior = PartyBehavior::Passive;
    travellers.roster = { t_recruit };
    c.factions.add(travellers);

    // Extensibility proof (V28): a whole faction in one block — sellswords
    // between the crowns. The blanket outlaw wars below make them bandit
    // hunters automatically; they keep the peace with every crown, so the
    // player meets them as parley partners (S4) and skirmish allies, not prey.
    FactionDef freecompany;
    freecompany.id = "freecompany"; freecompany.name = "Free Company";
    freecompany.color = Color{ 212, 175, 55, 255 };   // sellsword gold
    freecompany.behavior = PartyBehavior::Patrol;
    freecompany.roster = { t_pikeman, t_infantry, t_arbalist };
    freecompany.lords = { "Ostrec" };
    freecompany.lordPartySize = 150;   // TODO(balance)
    freecompany.mercenary = true;      // their steel is for sale (V29)
    c.factions.add(freecompany);

    // ---- Trade goods (direction E1) --------------------------------------
    // Stackable market wares. basePrice is a flat placeholder; per-town
    // spreads live on Town::priceOffset. TODO(balance): all prices.
    // `raw` goods are village produce (cheap at the source, dear in towns);
    // the rest is town craftwork flowing the other way — the E2 trade loop.
    c.goods.add({ "grain", "Grain",  10, true,  BEIGE });
    c.goods.add({ "wool",  "Wool",   10, true,  RAYWHITE });
    c.goods.add({ "iron",  "Iron",   10, true,  GRAY });
    c.goods.add({ "tools", "Tools",  10, false, LIGHTGRAY });
    c.goods.add({ "salt",  "Salt",   10, true,  Color{ 235, 235, 245, 255 } });
    c.goods.add({ "spice", "Spice",  10, false, ORANGE });
    // Extensibility proof (V3): a seventh ware in one line — villages fell
    // it cheap, towns pay dear, caravans haul it, events strike it.
    c.goods.add({ "timber", "Timber", 10, true, Color{ 120, 84, 50, 255 } });

    // ---- Enterprises (direction E4) --------------------------------------
    // One per town at most; bought at the market. TODO(balance): all numbers.
    c.enterprises.add({ "mill",     "Grain Mill", 300, 15, "grain" });
    c.enterprises.add({ "smithy",   "Smithy",     300, 15, "tools" });
    c.enterprises.add({ "dyeworks", "Dyeworks",   300, 15, "wool" });
    c.enterprises.add({ "sawmill",  "Sawmill",    300, 15, "timber" });

    // ---- Quests (direction F4) -------------------------------------------
    // Shapes only; givers rotate through them. TODO(balance): all rewards.
    c.quests.add({ "hunt",  "Bandit Hunt",
                   "Outlaws plague the roads. Break a band of them.",
                   QuestType::HuntBandits, 1, 100, 5, "" });
    c.quests.add({ "grain", "Grain Delivery",
                   "The granary runs thin. Bring grain from afar.",
                   QuestType::DeliverGrain, 5, 80, 5, "grain" });
    // V18 proof: a delivery quest for any ware — one registry line.
    c.quests.add({ "timber", "Timber for the Walls",
                   "The palisade rots. Bring timber for the wrights.",
                   QuestType::DeliverGrain, 4, 90, 5, "timber" });

    // ---- World events (R4) ------------------------------------------------
    // Fired on the day rotation at a rotating settlement, announced as news.
    // TODO(balance): every number.
    c.events.add({ "harvest", "Good Harvest",
                   "The harvest at %s is the best in years.", 20, 3, 0, 2 });
    c.events.add({ "murrain", "Murrain",
                   "A murrain strikes the herds at %s.", -20, -2, 0, -1 });
    c.events.add({ "banditking", "A Bandit King Rises",
                   "Masterless men flock to a bandit king near %s!", -10, 0, 2, -2 });

    // ---- Hero attributes (roadmap D3) ------------------------------------
    // Structure + intent only. No gameplay code reads these yet; the `hook`
    // strings are the contract for the balancing pass.
    c.attributes.add({ "str", "Strength",
                       "will modify: melee damage, carry weight" });
    c.attributes.add({ "agi", "Agility",
                       "will modify: move speed, swing speed, jump" });
    c.attributes.add({ "int", "Intelligence",
                       "will modify: troop XP gain, upgrade costs" });
    c.attributes.add({ "cha", "Charisma",
                       "will modify: recruit costs, party size, loot" });

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
    war(f_sarleon, c.playerFaction);   // two crowns, one land
    war(f_sarleon, f_patrol);          // ...and Sarleon presses its claim on the
                                       // old order too. The patrols stay at
                                       // peace with your warband only.
    war(f_vaeling, f_sarleon);         // the sea-kings raid the rival crown
    war(f_vaeling, f_patrol);          // ...and the old order's coasts alike;
                                       // they have no quarrel with you (yet).

    // ---- Overworld map (direction I1) ------------------------------------
    // Built-in default world, then the moddable overlay. assets/map.cfg ships
    // with a larger map; this fallback keeps the game running without assets.
    c.map = MapDef{};
    c.map.towns = {
        { { 400, 400 },   "Sargoth",  SettlementType::Town,    "player" },
        { { 1600, 500 },  "Praven",   SettlementType::Castle,  "patrol" },
        { { 500, 1550 },  "Tulga",    SettlementType::Village, "deserters" },
        { { 1500, 1500 }, "Jelkala",  SettlementType::Town,    "patrol" },
        { { 1000, 260 },  "Curaw",    SettlementType::Town,    "sarleon" },
        { { 260, 1000 },  "Rivacheg", SettlementType::Castle,  "vaeling" },
    };
    // Same lookup order as the font loader: beside the exe (CMake copies the
    // assets tree there), then the working directory and its parent.
    const std::string candidates[] = {
        IsWindowReady() ? std::string(GetApplicationDirectory()) + "assets/map.cfg"
                        : "assets/map.cfg",
        "assets/map.cfg", "../assets/map.cfg" };
    for (const std::string& p : candidates)
        if (FileExists(p.c_str())) { LoadMapConfig(c, p.c_str()); break; }
}

void LoadMapConfig(Content& c, const char* path) {
    std::ifstream f(path);
    if (!f) return;   // no cfg: the built-in default world stands

    MapDef m = c.map;
    bool clearedTowns = false;   // first `town` line replaces the default list
    std::string line;
    while (std::getline(f, line)) {
        if (const auto hash = line.find('#'); hash != std::string::npos)
            line.erase(hash);
        std::istringstream ss(line);
        std::string tag;
        if (!(ss >> tag)) continue;
        if (tag == "size")         ss >> m.size;
        else if (tag == "start")   ss >> m.playerStart.x >> m.playerStart.y;
        else if (tag == "parties") ss >> m.startingParties;
        else if (tag == "lair") {
            MapDef::LairSpec l;
            if (ss >> l.faction >> l.pos.x >> l.pos.y) m.lairs.push_back(l);
        }
        else if (tag == "biome") {
            // biome HILLFX HILLFY FORFX FORFY FORTHRESH MTNTHRESH FORSPD MTNSPD
            MapDef::BiomeSpec b = m.biome;
            if (ss >> b.hillFreqX >> b.hillFreqY >> b.forestFreqX >> b.forestFreqY
                   >> b.forestThreshold >> b.mountainThreshold
                   >> b.forestSpeed >> b.mountainSpeed)
                m.biome = b;
        }
        else if (tag == "road") ss >> m.roadLinkDist >> m.roadWidth;
        else if (tag == "lordnames") {
            std::vector<std::string> names;
            std::string n;
            while (ss >> n) names.push_back(n);
            if (!names.empty()) m.lordNames = names;
        }
        else if (tag == "town") {
            MapDef::TownSpec t;
            std::string type;
            if (!(ss >> t.name >> type >> t.pos.x >> t.pos.y >> t.owner)) continue;
            t.type = type == "village" ? SettlementType::Village
                   : type == "castle"  ? SettlementType::Castle
                                       : SettlementType::Town;
                if (!clearedTowns) { m.towns.clear(); clearedTowns = true; }
            m.towns.push_back(t);
        }
    }
    if (!m.towns.empty()) c.map = m;   // a map with no settlements is a mistake
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
