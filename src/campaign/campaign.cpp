#include "campaign.h"
#include "../save.h"
#include "../ui.h"
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Campaign map: top-down overworld. Your party moves toward the mouse (hold
// LMB) or with WASD. Other factions' parties roam by their own behaviour and
// may chase you; touching one starts a battle. Towns let you recruit from your
// faction's roster.
// ---------------------------------------------------------------------------

namespace {

constexpr float MAP_SIZE   = 2000.0f;
constexpr float PARTY_SPEED = 160.0f;

float Frand(float a, float b) {
    return a + (b - a) * (GetRandomValue(0, 10000) / 10000.0f);
}

// Build a party for a faction, filling its roster with a few troops each.
Party MakeParty(const Content& c, int faction, Vector2 pos) {
    Party p;
    p.faction = faction;
    p.pos = pos;
    p.wanderTarget = pos;
    p.troopCounts.assign(c.troops.size(), 0);
    for (int troop : c.factions[faction].roster)
        p.troopCounts[troop] = GetRandomValue(1, 4);
    return p;
}

Vector2 RandomEdgePos() {
    return { Frand(150, MAP_SIZE - 150), Frand(150, MAP_SIZE - 150) };
}

// Radius (in world units) within which a click counts as selecting a town.
constexpr float TOWN_CLICK_RADIUS = 36.0f;

const char* SettlementTypeName(SettlementType t) {
    switch (t) {
        case SettlementType::Village: return "Village";
        case SettlementType::Castle:  return "Castle";
        case SettlementType::Town:    return "Town";
    }
    return "Settlement";
}

// Non-player factions that can spawn as roaming parties.
std::vector<int> RoamingFactions(const Content& c) {
    std::vector<int> out;
    for (int i = 0; i < c.factions.size(); ++i)
        if (i != c.playerFaction) out.push_back(i);
    return out;
}

// --- party-vs-party warfare tuning (world-map scale, not battle balance) -----
constexpr float PARTY_COLLIDE_DIST = 22.0f;   // AI parties touch -> skirmish
constexpr float PLAYER_COLLIDE_DIST = 24.0f;  // AI party touches you -> battle
constexpr float PERCEPTION         = 500.0f;  // how far a party notices a foe
constexpr float SKIRMISH_TIME      = 6.0f;    // seconds a clash takes to resolve
constexpr float JOIN_RANGE         = 120.0f;  // how close to join a clash

// --- economy: world days, income, wages --------------------------------------
constexpr float DAY_LENGTH = 60.0f;   // TODO(balance): seconds of sim per day

// Daily income of an owned settlement. Relative sizes are settlement identity;
// TODO(balance): the numbers.
int SettlementIncome(SettlementType t) {
    switch (t) {
        case SettlementType::Village: return 20;
        case SettlementType::Town:    return 50;
        case SettlementType::Castle:  return 30;
    }
    return 0;
}

// --- lords (roadmap C3) ------------------------------------------------------
constexpr float SIEGE_REACH   = 44.0f;  // lord close enough to invest a town
constexpr float AI_SIEGE_TIME = 12.0f;  // seconds an AI siege takes to resolve
constexpr float LORD_RESPAWN  = 90.0f;  // TODO(balance): fallen lord downtime

// A potential opponent a roaming party has spotted. `index` is -1 for the
// player's party, >= 0 for gs.parties[index], or -2 when nothing is in range.
struct Foe {
    int     index    = -2;
    Vector2 pos{};
    int     strength = 0;
    float   dist     = PERCEPTION;
};

// A lord's host doesn't break march to chase parties this much smaller than
// itself — armies have wars to fight. TODO(balance): the contempt ratio.
constexpr int LORD_NOTICE_RATIO = 4;

// Nearest hostile party (player or AI) that party `ei` can currently see.
// Lords overlook prey "beneath their notice" (see LORD_NOTICE_RATIO).
Foe NearestHostile(const GameState& gs, const Content& c, int ei) {
    const Party& e = gs.parties[ei];
    const bool lordly = !e.lord.empty();
    const int  mine   = e.totalTroops();
    auto beneathNotice = [&](int strength) {
        return lordly && strength * LORD_NOTICE_RATIO < mine;
    };

    Foe best;
    if (AreFactionsHostile(c, e.faction, c.playerFaction) && gs.player.totalTroops() > 0 &&
        !beneathNotice(gs.player.totalTroops())) {
        const float d = Vector2Distance(e.pos, gs.player.pos);
        if (d < best.dist) best = { -1, gs.player.pos, gs.player.totalTroops(), d };
    }
    for (int j = 0; j < (int)gs.parties.size(); ++j) {
        if (j == ei) continue;
        const Party& o = gs.parties[j];
        if (!o.alive || o.engaged) continue;
        if (!AreFactionsHostile(c, e.faction, o.faction)) continue;
        if (beneathNotice(o.totalTroops())) continue;
        const float d = Vector2Distance(e.pos, o.pos);
        if (d < best.dist) best = { j, o.pos, o.totalTroops(), d };
    }
    return best;
}

// Where party `e` steers this tick, given the foe it has spotted (if any).
Vector2 SteerTarget(Party& e, PartyBehavior behavior, const Foe& foe, float sim) {
    if (foe.index != -2) {
        const bool couldWin = e.totalTroops() >= foe.strength / 2;
        switch (behavior) {
            case PartyBehavior::Aggressive:
                if (foe.dist < 500) return foe.pos;                 // hunts eagerly
                break;
            case PartyBehavior::Patrol:
                if (foe.dist < 300 && couldWin) return foe.pos;     // opportunistic
                break;
            case PartyBehavior::Passive:
                if (foe.dist < 220) {                               // flees
                    Vector2 away = Vector2Subtract(e.pos, foe.pos);
                    if (Vector2Length(away) > 1)
                        return Vector2Add(e.pos, Vector2Scale(Vector2Normalize(away), 200));
                }
                break;
        }
    }
    e.thinkTimer -= sim;
    if (e.thinkTimer <= 0) {
        e.wanderTarget = { Frand(100, MAP_SIZE - 100), Frand(100, MAP_SIZE - 100) };
        e.thinkTimer = Frand(3, 8);
    }
    return e.wanderTarget;
}

// Remove up to `count` troops from a party, spread across its troop types.
void RemoveTroops(Party& p, int count) {
    for (int guard = 0; guard < 10000 && count > 0 && p.totalTroops() > 0; ++guard) {
        const int t = GetRandomValue(0, (int)p.troopCounts.size() - 1);
        if (p.troopCounts[t] > 0) { p.troopCounts[t]--; count--; }
    }
}

// Auto-resolve a skirmish the player chose not to join: pick a winner weighted
// by strength, wipe the loser, and bloody the winner in proportion.
void ResolveSkirmish(GameState& gs, Skirmish& sk) {
    Party& a = gs.parties[sk.a];
    Party& b = gs.parties[sk.b];
    a.engaged = b.engaged = false;

    const int sa = a.totalTroops();
    const int sb = b.totalTroops();
    if (sa <= 0) { a.alive = false; return; }
    if (sb <= 0) { b.alive = false; return; }

    const bool aWins = Frand(0, (float)(sa + sb)) < (float)sa;   // stronger side favoured
    Party& winner = aWins ? a : b;
    Party& loser  = aWins ? b : a;
    const int loserStrength = aWins ? sb : sa;

    RemoveTroops(winner, GetRandomValue(loserStrength / 2, loserStrength));
    loser.alive = false;
    if (winner.totalTroops() <= 0) winner.alive = false;  // mutual annihilation
}

// Camera centred on the player; used by input gathering and drawing.
Camera2D CampaignCamera(const GameState& gs) {
    Camera2D cam = { 0 };
    cam.target = gs.player.pos;
    cam.offset = { GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f };
    cam.zoom = 1.0f;
    return cam;
}

// Nearest joinable skirmish, or -1. Used by update (join) and draw (prompt).
int NearestSkirmishIndex(const GameState& gs) {
    int best = -1;
    float bestD = JOIN_RANGE;
    for (int s = 0; s < (int)gs.skirmishes.size(); ++s) {
        const float d = Vector2Distance(gs.player.pos, gs.skirmishes[s].pos);
        if (d < bestD) { bestD = d; best = s; }
    }
    return best;
}

// A lord's army: lordPartySize troops round-robined over the faction roster.
Party MakeLordParty(const Content& c, int faction, const std::string& name, Vector2 pos) {
    Party p;
    p.faction = faction;
    p.lord = name;
    p.pos = p.wanderTarget = pos;
    p.troopCounts.assign(c.troops.size(), 0);
    const std::vector<int>& roster = c.factions[faction].roster;
    for (int i = 0; i < c.factions[faction].lordPartySize && !roster.empty(); ++i)
        p.troopCounts[roster[i % (int)roster.size()]]++;
    return p;
}

// Somewhere a faction can raise troops: an owned settlement, else a map edge.
Vector2 FactionHome(const GameState& gs, int faction) {
    for (const Town& t : gs.towns)
        if (t.owner == faction) return t.pos;
    return RandomEdgePos();
}

// Install a fresh garrison detached from `attacker` into a captured town.
void InstallGarrison(const Content& c, Town& t, Party& attacker) {
    int size = 8;                                          // TODO(balance)
    if (t.type == SettlementType::Village) size = 4;       // TODO(balance)
    if (t.type == SettlementType::Castle)  size = 12;      // TODO(balance)
    t.garrison.assign(c.troops.size(), 0);
    for (int i = 0; i < size; ++i) {
        // Detach from the attacker's most numerous type each pick, so the
        // garrison is a mix (archers man walls; a recruit-only garrison isn't).
        int best = -1, bestN = 0;
        for (int tr = 0; tr < (int)attacker.troopCounts.size(); ++tr)
            if (attacker.troopCounts[tr] > bestN) { best = tr; bestN = attacker.troopCounts[tr]; }
        if (best < 0) break;
        attacker.troopCounts[best]--;
        t.garrison[best]++;
    }
}

// Auto-resolve an AI siege: strength-weighted, like skirmishes. TODO(balance):
// defenders currently enjoy no walls bonus.
void ResolveAISiege(GameState& gs, AISiege& sg) {
    if (sg.party < 0 || sg.party >= (int)gs.parties.size()) return;
    Party& a = gs.parties[sg.party];
    a.engaged = false;
    if (!a.alive || sg.town < 0 || sg.town >= (int)gs.towns.size()) return;
    Town& t = gs.towns[sg.town];
    if (!AreFactionsHostile(gs.content, a.faction, t.owner)) return;  // already flipped

    const int sa = a.totalTroops();
    const int sd = t.garrisonSize();
    if (sa <= 0) { a.alive = false; return; }

    const bool taken = Frand(0, (float)(sa + sd)) < (float)sa;
    if (taken) {
        RemoveTroops(a, GetRandomValue(sd / 2, sd));   // storming has a price
        t.owner = a.faction;
        InstallGarrison(gs.content, t, a);
        if (a.totalTroops() <= 0) a.alive = false;
    } else {
        RemoveTroops(a, GetRandomValue(sa / 3, sa / 2));   // repelled, mauled
        if (a.totalTroops() <= 0) a.alive = false;
        // the garrison is bloodied too
        int loss = GetRandomValue(0, sd / 2);
        for (int tr = 0; tr < (int)t.garrison.size() && loss > 0; ++tr)
            while (t.garrison[tr] > 0 && loss > 0) { t.garrison[tr]--; loss--; }
    }
}

// Inventory grid geometry, shared by input gathering and drawing.
constexpr int INV_CELL = 52;
int InvOriginX() { return GetScreenWidth() / 2 - (INV_W * INV_CELL) / 2; }
int InvOriginY() { return 220; }

}  // namespace

// Defined with the inventory screen below; needed earlier for loot drops.
static bool AutoPlace(GameState& gs, InvItem it);

void CampaignInit(GameState& gs) {
    const Content& c = gs.content;

    // Player party + hero avatar.
    gs.player = Party{};
    gs.player.isPlayer = true;
    gs.player.faction = c.playerFaction;
    gs.player.pos = { MAP_SIZE / 2, MAP_SIZE / 2 };
    gs.player.troopCounts.assign(c.troops.size(), 0);
    if (const int r = c.troops.find("recruit");  r >= 0) gs.player.troopCounts[r] = 5;
    if (const int i = c.troops.find("infantry"); i >= 0) gs.player.troopCounts[i] = 2;

    // Give the hero a starting loadout (models are driven by these handles).
    gs.playerHero = Character{};
    gs.playerHero.attributes.assign(c.attributes.size(), 0);
    Loadout& hl = gs.playerHero.loadout;
    hl.set(EquipSlot::Head,   c.armor.find("helmet"));
    hl.set(EquipSlot::Body,   c.armor.find("plate"));
    hl.set(EquipSlot::Hands,  c.armor.find("gloves"));
    hl.set(EquipSlot::Feet,   c.armor.find("boots"));
    hl.addWeapon(c.weapons.find("sword"));   // the hero carries several weapons;
    hl.addWeapon(c.weapons.find("spear"));   // swap between them in battle with Q
    hl.addWeapon(c.weapons.find("great"));

    // Who holds what at the start: your warband holds Sargoth, the lawful
    // patrols hold the castle and Jelkala, and deserters squat in Tulga —
    // a hostile settlement on the map from day one (siege bait for B3).
    const int f_kingdom   = c.playerFaction;
    const int f_patrol    = c.factions.find("patrol");
    const int f_deserters = c.factions.find("deserters");
    const int f_sarleon   = c.factions.find("sarleon");
    gs.towns = {
        { { 400, 400 },   "Sargoth",  SettlementType::Town,    f_kingdom },
        { { 1600, 500 },  "Praven",   SettlementType::Castle,  f_patrol },
        { { 500, 1550 },  "Tulga",    SettlementType::Village, f_deserters },
        { { 1500, 1500 }, "Jelkala",  SettlementType::Town,    f_patrol },
        { { 1000, 260 },  "Curaw",    SettlementType::Town,    f_sarleon },
    };

    // Garrison every owned settlement from its owner's roster. Relative sizes
    // are settlement identity (a castle holds more than a village);
    // TODO(balance): the actual numbers.
    for (Town& t : gs.towns) {
        t.garrison.assign(c.troops.size(), 0);
        if (t.owner < 0) continue;
        int size = 8;                                          // TODO(balance)
        if (t.type == SettlementType::Village) size = 4;       // TODO(balance)
        if (t.type == SettlementType::Castle)  size = 12;      // TODO(balance)
        const std::vector<int>& roster = c.factions[t.owner].roster;
        for (int i = 0; i < size && !roster.empty(); ++i)
            t.garrison[roster[i % (int)roster.size()]]++;
    }

    gs.parties.clear();
    gs.skirmishes.clear();
    gs.aiSieges.clear();
    gs.lordRespawns.clear();
    gs.playerLosses.assign(c.troops.size(), 0);
    gs.troopXp.assign(c.troops.size(), 0);
    gs.prisoners.assign(c.troops.size(), 0);
    const std::vector<int> roamers = RoamingFactions(c);
    for (int i = 0; i < 5; ++i)
        gs.parties.push_back(MakeParty(c, roamers[i % roamers.size()], RandomEdgePos()));

    // Lords muster their hosts at a settlement their faction holds.
    for (int f = 0; f < c.factions.size(); ++f)
        for (const std::string& name : c.factions[f].lords)
            gs.parties.push_back(MakeLordParty(c, f, name, FactionHome(gs, f)));
}

// A party index is usable as a live entry in gs.parties.
static bool ValidParty(const GameState& gs, int i) {
    return i >= 0 && i < (int)gs.parties.size();
}

// "2 Recruit, 1 Archer" — human-readable troop tally; empty when no losses.
static std::string LossSummary(const Content& c, const std::vector<int>& losses) {
    std::string out;
    for (int t = 0; t < (int)losses.size() && t < c.troops.size(); ++t) {
        if (losses[t] <= 0) continue;
        if (!out.empty()) out += ", ";
        out += TextFormat("%d %s", losses[t], c.troops[t].name.c_str());
    }
    return out;
}

// Experience each surviving troop earns from a won battle. TODO(balance).
static constexpr int XP_PER_SURVIVOR = 25;
// Hero experience per won battle, and XP needed per level. TODO(balance).
static constexpr int HERO_XP_PER_WIN = 50;
static int HeroXpToLevel(int level) { return level * 100; }

static void ApplyBattleResult(GameState& gs) {
    // Player's own casualties.
    for (int t = 0; t < (int)gs.player.troopCounts.size(); ++t) {
        gs.player.troopCounts[t] -= gs.playerLosses[t];
        if (gs.player.troopCounts[t] < 0) gs.player.troopCounts[t] = 0;
    }

    // Veterancy: survivors of a won battle season toward their next rank.
    if (gs.battleWon) {
        if ((int)gs.troopXp.size() < gs.content.troops.size())
            gs.troopXp.assign(gs.content.troops.size(), 0);
        for (int t = 0; t < (int)gs.player.troopCounts.size(); ++t)
            gs.troopXp[t] += gs.player.troopCounts[t] * XP_PER_SURVIVOR;

        // The hero grows too: XP, levels, and attribute points to spend.
        Character& hero = gs.playerHero;
        hero.xp += HERO_XP_PER_WIN;
        while (hero.xp >= HeroXpToLevel(hero.level)) {
            hero.xp -= HeroXpToLevel(hero.level);
            hero.level++;
            hero.attrPoints++;
        }
    }

    // Allied party's casualties, if one fought alongside you.
    Party* ally = ValidParty(gs, gs.battleAllyIndex) ? &gs.parties[gs.battleAllyIndex] : nullptr;
    if (ally) {
        ally->engaged = false;
        for (int t = 0; t < (int)gs.allyLosses.size() && t < (int)ally->troopCounts.size(); ++t) {
            ally->troopCounts[t] -= gs.allyLosses[t];
            if (ally->troopCounts[t] < 0) ally->troopCounts[t] = 0;
        }
    }

    Party* enemy = ValidParty(gs, gs.battlePartyIndex) ? &gs.parties[gs.battlePartyIndex] : nullptr;
    if (enemy) enemy->engaged = false;

    // Siege outcome: the garrison takes its casualties; a captured settlement
    // changes hands (villages are "sacked", walls are "stormed").
    if (gs.siegeTownIndex >= 0 && gs.siegeTownIndex < (int)gs.towns.size()) {
        Town& t = gs.towns[gs.siegeTownIndex];
        for (int i = 0; i < (int)t.garrison.size() && i < (int)gs.enemyLosses.size(); ++i) {
            t.garrison[i] -= gs.enemyLosses[i];
            if (t.garrison[i] < 0) t.garrison[i] = 0;
        }
        if (gs.battleWon) {
            t.owner = gs.content.playerFaction;
            const int loot = 50 + GetRandomValue(0, 100);   // TODO(balance)
            gs.gold += loot;
            gs.resultText = t.type == SettlementType::Village
                ? TextFormat("%s IS SACKED!  It flies your banner now. Loot: %d gold",
                             t.name.c_str(), loot)
                : TextFormat("%s IS TAKEN!  The %s is yours. Loot: %d gold",
                             t.name.c_str(), SettlementTypeName(t.type), loot);
        } else {
            gs.resultText = TextFormat("The assault on %s is repelled...", t.name.c_str());
            gs.player.pos.x = Clamp(gs.player.pos.x + Frand(-300, 300), 100, MAP_SIZE - 100);
            gs.player.pos.y = Clamp(gs.player.pos.y + Frand(-300, 300), 100, MAP_SIZE - 100);
        }
        const std::string fallenS = LossSummary(gs.content, gs.playerLosses);
        if (!fallenS.empty()) gs.resultText += "   Fallen: " + fallenS;
        gs.siegeTownIndex   = -1;
        gs.battlePartyIndex = -1;
        gs.battleAllyIndex  = -1;
        gs.allyLosses.clear();
        return;
    }

    // Battle report: outcome, loot, and what the fight cost each side.
    const std::string fallen = LossSummary(gs.content, gs.playerLosses);
    const std::string slain  = LossSummary(gs.content, gs.enemyLosses);
    if (gs.battleWon) {
        const int loot = 50 + GetRandomValue(0, 100);
        gs.gold += loot;
        gs.resultText = ally ? TextFormat("VICTORY!  Your ally holds the field. Loot: %d gold", loot)
                             : TextFormat("VICTORY!  Loot: %d gold", loot);
        if (enemy) enemy->alive = false;

        // A share of the beaten foe yields rather than dies — captives to
        // ransom at a friendly tavern. TODO(balance): the capture share.
        if ((int)gs.prisoners.size() < gs.content.troops.size())
            gs.prisoners.assign(gs.content.troops.size(), 0);
        int captives = 0;
        for (int t = 0; t < (int)gs.enemyLosses.size() && t < gs.content.troops.size(); ++t) {
            const int took = gs.enemyLosses[t] > 0 ? (gs.enemyLosses[t] * 3 + 9) / 10 : 0;
            gs.prisoners[t] += took;
            captives += took;
        }
        if (captives > 0)
            gs.resultText += TextFormat("   Captives: %d", captives);

        // Battlefield pickings: a fallen foe's gear sometimes ends up in the
        // bag. TODO(balance): drop chance.
        if (GetRandomValue(0, 99) < 50) {
            InvItem drop;
            drop.isWeapon = GetRandomValue(0, 1) == 1;
            drop.handle = drop.isWeapon ? GetRandomValue(0, gs.content.weapons.size() - 1)
                                        : GetRandomValue(0, gs.content.armor.size() - 1);
            if (AutoPlace(gs, drop)) {
                const char* nm = drop.isWeapon ? gs.content.weapons[drop.handle].name.c_str()
                                               : gs.content.armor[drop.handle].name.c_str();
                gs.resultText += TextFormat("   Picked up: %s", nm);
            }
        }
    } else {
        gs.resultText = "DEFEAT...  You escape with the survivors.";
        gs.player.pos.x = Clamp(gs.player.pos.x + Frand(-300, 300), 100, MAP_SIZE - 100);
        gs.player.pos.y = Clamp(gs.player.pos.y + Frand(-300, 300), 100, MAP_SIZE - 100);
        if (ally) ally->alive = false;   // the side you backed lost the field
    }
    if (!slain.empty())  gs.resultText += "   Slain: " + slain;
    if (!fallen.empty()) gs.resultText += "   Fallen: " + fallen;

    // A party wiped out of troops is gone regardless of who "won".
    if (ally && ally->totalTroops() <= 0) ally->alive = false;

    gs.battlePartyIndex = -1;
    gs.battleAllyIndex  = -1;
    gs.allyLosses.clear();
}

// Read the real devices into campaign intent. Windowed play only — the
// headless harness builds CampaignInput directly.
CampaignInput GatherCampaignInput(const GameState& gs) {
    CampaignInput in;

    if (gs.screen == Screen::Settlement) {
        // Walking a settlement: menu intents only — movement is gathered
        // separately via GatherBattleInput (same third-person controls).
        const std::vector<int>& roster =
            gs.content.factions[gs.content.playerFaction].roster;
        for (int slot = 0; slot < (int)roster.size(); ++slot)
            if (IsKeyPressed(KEY_ONE + slot)) in.recruitSlot = slot;
        in.ransom = IsKeyPressed(KEY_R);
        if (IsKeyPressed(KEY_ESCAPE)) in.leaveSettlement = true;
        return in;
    }

    if (gs.screen == Screen::Title) {
        if (IsKeyPressed(KEY_N))      in.menuChoice = 1;
        if (IsKeyPressed(KEY_C))      in.menuChoice = 2;
        if (IsKeyPressed(KEY_ESCAPE)) in.menuChoice = 3;
        return in;
    }

    if (gs.screen == Screen::Victory) {
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER)) in.leaveSettlement = true;
        return in;
    }

    if (gs.screen == Screen::Party) {
        const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        for (int slot = 0; slot < 9; ++slot)
            if (IsKeyPressed(KEY_ONE + slot))
                (shift ? in.dismissSlot : in.upgradeSlot) = slot;
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P)) in.leaveSettlement = true;
        return in;
    }

    if (gs.screen == Screen::Character) {
        for (int slot = 0; slot < 9; ++slot)
            if (IsKeyPressed(KEY_ONE + slot)) in.spendAttr = slot;
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_C)) in.leaveSettlement = true;
        return in;
    }

    if (gs.screen == Screen::Inventory) {
        const Vector2 m = GetMousePosition();
        const int cx = ((int)m.x - InvOriginX()) / INV_CELL;
        const int cy = ((int)m.y - InvOriginY()) / INV_CELL;
        if (cx >= 0 && cx < INV_W && cy >= 0 && cy < INV_H &&
            m.x >= InvOriginX() && m.y >= InvOriginY()) {
            in.invCellX = cx;
            in.invCellY = cy;
        }
        in.invPick  = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        in.invEquip = IsKeyPressed(KEY_E) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_I)) in.leaveSettlement = true;
        return in;
    }

    const Camera2D cam = CampaignCamera(gs);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        const Vector2 world = GetScreenToWorld2D(GetMousePosition(), cam);
        for (int i = 0; i < (int)gs.towns.size(); ++i)
            if (Vector2Distance(world, gs.towns[i].pos) < TOWN_CLICK_RADIUS)
                in.clickSettlement = i;
    }
    if (IsKeyDown(KEY_W)) in.move.y -= 1;
    if (IsKeyDown(KEY_S)) in.move.y += 1;
    if (IsKeyDown(KEY_A)) in.move.x -= 1;
    if (IsKeyDown(KEY_D)) in.move.x += 1;
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        const Vector2 world = GetScreenToWorld2D(GetMousePosition(), cam);
        const Vector2 dir = Vector2Subtract(world, gs.player.pos);
        if (Vector2Length(dir) > 8) in.move = Vector2Normalize(dir);
    }
    in.wait = IsKeyDown(KEY_SPACE);
    if (IsKeyPressed(KEY_ONE)) in.joinSide = 1;
    if (IsKeyPressed(KEY_TWO)) in.joinSide = 2;
    in.restart   = IsKeyPressed(KEY_R);
    in.openParty = IsKeyPressed(KEY_P);
    in.openInventory = IsKeyPressed(KEY_I);
    in.openCharacter = IsKeyPressed(KEY_C);
    in.quickSave = IsKeyPressed(KEY_F5);
    in.quickLoad = IsKeyPressed(KEY_F9);
    return in;
}

void CampaignUpdate(GameState& gs, float dt, const CampaignInput& in) {
    const Content& c = gs.content;

    if (gs.screen == Screen::BattleResult) {
        ApplyBattleResult(gs);
        gs.screen = Screen::Campaign;
    }

    // ---- restart after the warband is destroyed ----
    if (gs.player.totalTroops() == 0 && in.restart) {
        Content saved = std::move(gs.content);
        gs = GameState{};
        gs.content = std::move(saved);
        CampaignInit(gs);
        return;
    }

    // ---- quicksave / quickload ----
    if (in.quickSave) {
        gs.resultText = SaveGame(gs, DefaultSavePath()) ? "Game saved."
                                                        : "Save FAILED.";
    }
    if (in.quickLoad) {
        if (LoadGame(gs, DefaultSavePath())) { gs.resultText = "Game loaded."; return; }
        gs.resultText = "No save to load.";
    }

    // ---- open the party management / inventory screens ----
    if (in.openParty) {
        gs.screen = Screen::Party;
        return;
    }
    if (in.openInventory) {
        gs.screen = Screen::Inventory;
        return;
    }
    if (in.openCharacter) {
        gs.screen = Screen::Character;
        return;
    }

    // ---- enter (friendly) or assault (hostile) a settlement ----
    if (in.clickSettlement >= 0 && in.clickSettlement < (int)gs.towns.size()) {
        Town& t = gs.towns[in.clickSettlement];
        if (AreFactionsHostile(c, t.owner, c.playerFaction)) {
            if (t.garrisonSize() <= 0) {
                // Nobody mans the walls — it simply changes hands.
                t.owner = c.playerFaction;
                gs.resultText = TextFormat("%s is undefended. It is yours.", t.name.c_str());
            } else if (gs.player.totalTroops() > 0) {
                // Storm it: the garrison fights on its home ground.
                gs.siegeTownIndex   = in.clickSettlement;
                gs.battlePartyIndex = -1;
                gs.battleAllyIndex  = -1;
                gs.screen = Screen::Battle;
                return;
            }
        } else {
            gs.currentSettlement = in.clickSettlement;
            gs.screen = Screen::Settlement;
            return;
        }
    }

    // ---- the world clock only ticks while you act ----
    // Travelling is an action, so time flows while you move. Standing still
    // freezes the whole overworld; wait (SPACE) lets time pass without moving.
    Vector2 move = in.move;
    const bool moving  = Vector2Length(move) > 0;
    gs.timeFlowing = moving || in.wait;
    const float sim = gs.timeFlowing ? dt : 0.0f;

    if (moving) {
        move = Vector2Normalize(move);
        gs.player.pos = Vector2Add(gs.player.pos, Vector2Scale(move, PARTY_SPEED * dt));
    }
    gs.player.pos.x = Clamp(gs.player.pos.x, 0, MAP_SIZE);
    gs.player.pos.y = Clamp(gs.player.pos.y, 0, MAP_SIZE);

    // ---- world simulation (advances only while time flows) ----
    if (sim > 0.0f) {
        // Roaming + pursuit: each free party steers by its behaviour toward the
        // nearest hostile party it can see (the player, or another faction).
        for (int i = 0; i < (int)gs.parties.size(); ++i) {
            Party& e = gs.parties[i];
            if (!e.alive || e.engaged) continue;

            Vector2 target;
            bool haveTarget = false;

            // Lords wage the settlement war: march on the nearest hostile
            // settlement they outmatch and invest it on arrival.
            if (!e.lord.empty()) {
                int  bestTown = -1;
                float bestD   = 1e9f;
                for (int ti = 0; ti < (int)gs.towns.size(); ++ti) {
                    const Town& t = gs.towns[ti];
                    if (!AreFactionsHostile(c, e.faction, t.owner)) continue;
                    if (t.garrisonSize() * 2 > e.totalTroops()) continue;  // TODO(balance)
                    const float d = Vector2Distance(e.pos, t.pos);
                    if (d < bestD) { bestD = d; bestTown = ti; }
                }
                if (bestTown >= 0) {
                    if (bestD < SIEGE_REACH) {
                        e.engaged = true;
                        gs.aiSieges.push_back({ i, bestTown, AI_SIEGE_TIME });
                        // Sound the alarm when it's YOUR settlement invested.
                        if (gs.towns[bestTown].owner == c.playerFaction)
                            gs.resultText = TextFormat("%s IS UNDER SIEGE by Lord %s!",
                                                       gs.towns[bestTown].name.c_str(),
                                                       e.lord.c_str());
                        continue;
                    }
                    target = gs.towns[bestTown].pos;
                    haveTarget = true;
                }
            }

            if (!haveTarget) {
                const PartyBehavior behavior = c.factions[e.faction].behavior;
                const Foe foe = NearestHostile(gs, c, i);
                target = SteerTarget(e, behavior, foe, sim);
            }
            const Vector2 dir = Vector2Subtract(target, e.pos);
            if (Vector2Length(dir) > 5)
                e.pos = Vector2Add(e.pos, Vector2Scale(Vector2Normalize(dir), PARTY_SPEED * 0.7f * sim));
            // Keep parties on the map — a fleeing party would otherwise run off
            // the edge forever (unreachable, yet still counted as alive).
            e.pos.x = Clamp(e.pos.x, 0, MAP_SIZE);
            e.pos.y = Clamp(e.pos.y, 0, MAP_SIZE);
        }

        // Two hostile AI parties touching -> they lock into a skirmish.
        for (int i = 0; i < (int)gs.parties.size(); ++i) {
            Party& a = gs.parties[i];
            if (!a.alive || a.engaged) continue;
            for (int j = i + 1; j < (int)gs.parties.size(); ++j) {
                Party& b = gs.parties[j];
                if (!b.alive || b.engaged) continue;
                if (!AreFactionsHostile(c, a.faction, b.faction)) continue;
                if (Vector2Distance(a.pos, b.pos) < PARTY_COLLIDE_DIST) {
                    a.engaged = b.engaged = true;
                    Skirmish sk;
                    sk.a = i; sk.b = j;
                    sk.timer = sk.duration = SKIRMISH_TIME;
                    sk.pos = Vector2Scale(Vector2Add(a.pos, b.pos), 0.5f);
                    gs.skirmishes.push_back(sk);
                }
            }
        }

        // A hostile AI party touching YOU -> your own battle. A party besieging
        // a settlement can be attacked too — riding into it breaks the siege.
        for (int i = 0; i < (int)gs.parties.size(); ++i) {
            Party& e = gs.parties[i];
            if (!e.alive) continue;
            int besiegingIdx = -1;
            if (e.engaged) {
                for (int s = 0; s < (int)gs.aiSieges.size(); ++s)
                    if (gs.aiSieges[s].party == i) { besiegingIdx = s; break; }
                if (besiegingIdx < 0) continue;   // locked in a skirmish; join it instead
            }
            if (AreFactionsHostile(c, e.faction, c.playerFaction) &&
                gs.player.totalTroops() > 0 &&
                Vector2Distance(e.pos, gs.player.pos) < PLAYER_COLLIDE_DIST) {
                if (besiegingIdx >= 0) {           // you fall upon the siege camp
                    gs.aiSieges.erase(gs.aiSieges.begin() + besiegingIdx);
                    e.engaged = false;
                }
                gs.battlePartyIndex = i;
                gs.battleAllyIndex  = -1;
                gs.screen = Screen::Battle;
                return;
            }
        }

        // Auto-resolve clashes the player left alone; drop the finished ones.
        for (Skirmish& sk : gs.skirmishes) {
            sk.timer -= sim;
            if (sk.timer <= 0) ResolveSkirmish(gs, sk);
        }
        gs.skirmishes.erase(
            std::remove_if(gs.skirmishes.begin(), gs.skirmishes.end(),
                           [](const Skirmish& s) { return s.timer <= 0; }),
            gs.skirmishes.end());

        // AI sieges run their course the same way.
        for (AISiege& sg : gs.aiSieges) {
            sg.timer -= sim;
            if (sg.timer <= 0) ResolveAISiege(gs, sg);
        }
        gs.aiSieges.erase(
            std::remove_if(gs.aiSieges.begin(), gs.aiSieges.end(),
                           [](const AISiege& s) { return s.timer <= 0; }),
            gs.aiSieges.end());

        // Fallen lords raise a new host at home after a while.
        for (Party& e : gs.parties) {
            if (e.alive || e.lord.empty()) continue;
            gs.lordRespawns.push_back({ e.faction, e.lord, LORD_RESPAWN });
            e.lord.clear();   // queued exactly once
        }
        for (LordRespawn& lr : gs.lordRespawns) {
            lr.timer -= sim;
            if (lr.timer <= 0)
                gs.parties.push_back(MakeLordParty(c, lr.faction, lr.name,
                                                   FactionHome(gs, lr.faction)));
        }
        gs.lordRespawns.erase(
            std::remove_if(gs.lordRespawns.begin(), gs.lordRespawns.end(),
                           [](const LordRespawn& r) { return r.timer <= 0; }),
            gs.lordRespawns.end());

        // ---- victory: every banner on the map is yours ----
        {
            bool all = !gs.towns.empty();
            for (const Town& t : gs.towns)
                if (t.owner != c.playerFaction) { all = false; break; }
            if (all) {
                gs.screen = Screen::Victory;
                return;
            }
        }

        // ---- the daily ledger: settlement income in, troop wages out ----
        gs.dayTimer += sim;
        if (gs.dayTimer >= DAY_LENGTH) {
            gs.dayTimer -= DAY_LENGTH;
            gs.day++;
            int income = 0;
            for (const Town& t : gs.towns)
                if (t.owner == c.playerFaction) income += SettlementIncome(t.type);
            int wages = 0;
            for (int t = 0; t < (int)gs.player.troopCounts.size(); ++t)
                wages += gs.player.troopCounts[t] * c.troops[t].wage;
            gs.gold += income - wages;
            gs.resultText = TextFormat("Day %d:  +%d from your lands, -%d in wages.",
                                       gs.day, income, wages);
            if (gs.gold < 0) {
                // The coffers ran dry: a share of the men drift away overnight.
                gs.gold = 0;
                const int leave = (gs.player.totalTroops() + 9) / 10;   // TODO(balance)
                RemoveTroops(gs.player, leave);
                gs.resultText += TextFormat("  Unpaid, %d men desert!", leave);
            }
        }

        // Respawn parties over time so the map stays lively.
        gs.spawnTimer += sim;
        int aliveCount = 0;
        for (const auto& e : gs.parties) if (e.alive) aliveCount++;
        if (gs.spawnTimer > 20 && aliveCount < 8) {
            gs.spawnTimer = 0;
            const std::vector<int> roamers = RoamingFactions(c);
            gs.parties.push_back(MakeParty(c, roamers[GetRandomValue(0, (int)roamers.size() - 1)], RandomEdgePos()));
        }
    }

    // ---- join a nearby clash on one side (a decision; works even while paused) ----
    const int nearSkirmish = NearestSkirmishIndex(gs);
    if (nearSkirmish >= 0 && gs.player.totalTroops() > 0 && in.joinSide != 0) {
        const Skirmish& sk = gs.skirmishes[nearSkirmish];
        int allyIdx = -1, enemyIdx = -1;
        if (in.joinSide == 1) { allyIdx = sk.a; enemyIdx = sk.b; }
        if (in.joinSide == 2) { allyIdx = sk.b; enemyIdx = sk.a; }
        if (allyIdx >= 0) {
            gs.battleAllyIndex  = allyIdx;
            gs.battlePartyIndex = enemyIdx;
            gs.skirmishes.erase(gs.skirmishes.begin() + nearSkirmish);
            gs.screen = Screen::Battle;
            return;
        }
    }
}

void CampaignDraw(const GameState& gs) {
    const Content& c = gs.content;
    const Camera2D cam = CampaignCamera(gs);
    const int nearSkirmish = NearestSkirmishIndex(gs);

    BeginDrawing();
    ClearBackground(Color{ 40, 58, 36, 255 });   // beyond the map's edge

    BeginMode2D(cam);
    // Painted ground: biome patches from the SAME noise that shapes battle
    // terrain, so the map foreshadows the battlefield you'd fight on.
    constexpr int CELL = 100;
    for (int gy = 0; gy < (int)MAP_SIZE / CELL; ++gy) {
        for (int gx = 0; gx < (int)MAP_SIZE / CELL; ++gx) {
            const float wx = gx * CELL + CELL * 0.5f;
            const float wy = gy * CELL + CELL * 0.5f;
            const float n1 = sinf(wx * 0.0031f) * cosf(wy * 0.0027f);   // hills
            const float n2 = sinf(wx * 0.0012f + wy * 0.0019f);         // forests
            const Color ground = { (unsigned char)(66 + 20.0f * n1),
                                   (unsigned char)(98 + 14.0f * n2),
                                   (unsigned char)(48 + 10.0f * n1), 255 };
            DrawRectangle(gx * CELL, gy * CELL, CELL, CELL, ground);

            unsigned int h = (unsigned)(gx * 73856093) ^ (unsigned)(gy * 19349663);
            h ^= h >> 13;
            if (n2 > 0.35f) {   // forest clumps
                for (int t = 0; t < 5; ++t) {
                    h = h * 1664525u + 1013904223u;
                    const float tx = gx * CELL + (h & 63) + 16;
                    const float ty = gy * CELL + ((h >> 8) & 63) + 16;
                    DrawTriangle({ tx, ty }, { tx + 10, ty }, { tx + 5, ty - 16 },
                                 Color{ 34, 66, 34, 255 });
                }
            } else if (n1 > 0.55f) {   // mountain ridges
                for (int t = 0; t < 3; ++t) {
                    h = h * 1664525u + 1013904223u;
                    const float tx = gx * CELL + (h & 63) + 12;
                    const float ty = gy * CELL + ((h >> 8) & 63) + 24;
                    DrawTriangle({ tx, ty }, { tx + 22, ty }, { tx + 11, ty - 20 },
                                 Color{ 118, 112, 108, 255 });
                    DrawTriangle({ tx + 7, ty - 12 }, { tx + 15, ty - 12 },
                                 { tx + 11, ty - 20 }, Color{ 226, 228, 232, 255 });
                }
            }
        }
    }

    // Roads thread the settlements together.
    for (int a = 0; a < (int)gs.towns.size(); ++a)
        for (int b = a + 1; b < (int)gs.towns.size(); ++b)
            if (Vector2Distance(gs.towns[a].pos, gs.towns[b].pos) < 1350)
                DrawLineEx(gs.towns[a].pos, gs.towns[b].pos, 5,
                           Fade(Color{ 128, 106, 76, 255 }, 0.45f));

    DrawRectangleLinesEx(Rectangle{ 0, 0, MAP_SIZE, MAP_SIZE }, 6, DARKBROWN);

    for (const Town& t : gs.towns) {
        DrawEllipse((int)t.pos.x, (int)t.pos.y + 16, 30, 9, Fade(BLACK, 0.25f));
        switch (t.type) {
            case SettlementType::Village:
                DrawRectangle((int)t.pos.x - 14, (int)t.pos.y - 10, 28, 20, BROWN);
                DrawTriangle({ t.pos.x - 16, t.pos.y - 10 }, { t.pos.x + 16, t.pos.y - 10 },
                             { t.pos.x, t.pos.y - 26 }, DARKBROWN);
                break;
            case SettlementType::Town:
                DrawRectangle((int)t.pos.x - 20, (int)t.pos.y - 20, 40, 40, BROWN);
                DrawTriangle({ t.pos.x - 24, t.pos.y - 20 }, { t.pos.x + 24, t.pos.y - 20 },
                             { t.pos.x, t.pos.y - 44 }, MAROON);
                break;
            case SettlementType::Castle:
                DrawRectangle((int)t.pos.x - 24, (int)t.pos.y - 22, 48, 42, GRAY);
                // crenellated top
                for (int b = -24; b < 24; b += 16)
                    DrawRectangle((int)t.pos.x + b, (int)t.pos.y - 34, 8, 12, DARKGRAY);
                break;
        }
        // Owner banner flying above the icon + label + ring.
        const bool ownerValid = t.owner >= 0 && t.owner < c.factions.size();
        const Color ownerCol  = ownerValid ? c.factions[t.owner].color : RAYWHITE;
        if (ownerValid) {
            const float bx = t.pos.x + 22, by = t.pos.y - 34;
            DrawLineEx({ bx, by + 22 }, { bx, by }, 2.5f, Color{ 92, 70, 48, 255 });
            DrawRectangle((int)bx, (int)by, 16, 10, ownerCol);
            DrawRectangleLines((int)bx, (int)by, 16, 10, Fade(BLACK, 0.5f));
        }
        ui::Text(TextFormat("%s (%s)", t.name.c_str(), SettlementTypeName(t.type)),
                 (int)t.pos.x - 40, (int)t.pos.y + 26, 16, RAYWHITE);
        if (ownerValid)
            ui::Text(TextFormat("%s (%d)", c.factions[t.owner].name.c_str(), t.garrisonSize()),
                     (int)t.pos.x - 40, (int)t.pos.y + 44, 14, ownerCol);
        DrawCircleLines((int)t.pos.x, (int)t.pos.y, TOWN_CLICK_RADIUS, Fade(ownerCol, 0.45f));
    }

    for (const Party& e : gs.parties) {
        if (!e.alive) continue;
        const FactionDef& f = c.factions[e.faction];
        const bool isLord = !e.lord.empty();
        // A pennant: shadow, pole, and a swallow-tailed flag in faction colour.
        DrawEllipse((int)e.pos.x, (int)e.pos.y + 4, 10, 4, Fade(BLACK, 0.3f));
        DrawLineEx({ e.pos.x, e.pos.y + 4 }, { e.pos.x, e.pos.y - (isLord ? 26.0f : 20.0f) },
                   2.5f, Color{ 92, 70, 48, 255 });
        const float fy = e.pos.y - (isLord ? 26.0f : 20.0f);
        const float fw = isLord ? 18.0f : 13.0f;
        DrawTriangle({ e.pos.x, fy }, { e.pos.x, fy + 10 }, { e.pos.x + fw, fy + 5 }, f.color);
        DrawTriangleLines({ e.pos.x, fy }, { e.pos.x, fy + 10 }, { e.pos.x + fw, fy + 5 },
                          Fade(BLACK, 0.5f));
        if (isLord)   // a small crown atop the pole
            DrawTriangle({ e.pos.x - 6, fy - 2 }, { e.pos.x + 6, fy - 2 },
                         { e.pos.x, fy - 10 }, GOLD);
        if (e.engaged) DrawCircleLines((int)e.pos.x, (int)e.pos.y, 15, RAYWHITE);
        ui::Text(isLord ? TextFormat("Lord %s %d", e.lord.c_str(), e.totalTroops())
                        : TextFormat("%s %d", f.name.c_str(), e.totalTroops()),
                 (int)e.pos.x - 20, (int)e.pos.y - (isLord ? 44 : 36), 14, f.color);
    }

    // Besieged settlements: a pulsing ring in the attacker's colour.
    for (const AISiege& sg : gs.aiSieges) {
        if (sg.town < 0 || sg.town >= (int)gs.towns.size()) continue;
        if (sg.party < 0 || sg.party >= (int)gs.parties.size()) continue;
        const Town& t = gs.towns[sg.town];
        DrawCircleLines((int)t.pos.x, (int)t.pos.y, TOWN_CLICK_RADIUS + 10,
                        c.factions[gs.parties[sg.party].faction].color);
        ui::Text("UNDER SIEGE", (int)t.pos.x - 44, (int)t.pos.y - 64, 16, RED);
    }

    // Ongoing clashes: crossed swords, the two factions' colours, and a ring
    // that empties as the fight nears its outcome.
    for (const Skirmish& sk : gs.skirmishes) {
        const FactionDef& fa = c.factions[gs.parties[sk.a].faction];
        const FactionDef& fb = c.factions[gs.parties[sk.b].faction];
        DrawCircleV(sk.pos, 22, Fade(BLACK, 0.35f));
        DrawLineEx({ sk.pos.x - 12, sk.pos.y - 12 }, { sk.pos.x + 12, sk.pos.y + 12 }, 3, fa.color);
        DrawLineEx({ sk.pos.x + 12, sk.pos.y - 12 }, { sk.pos.x - 12, sk.pos.y + 12 }, 3, fb.color);
        const float frac = sk.duration > 0 ? sk.timer / sk.duration : 0;
        DrawRing(sk.pos, 24, 27, -90, -90 + 360 * frac, 32, Fade(GOLD, 0.9f));
    }

    // Your own banner: taller pole, bright flag, gold finial.
    {
        const Vector2 p = gs.player.pos;
        const Color mine = c.factions[c.playerFaction].color;
        DrawEllipse((int)p.x, (int)p.y + 4, 11, 4, Fade(BLACK, 0.3f));
        DrawLineEx({ p.x, p.y + 4 }, { p.x, p.y - 30 }, 3.0f, Color{ 92, 70, 48, 255 });
        DrawTriangle({ p.x, p.y - 30 }, { p.x, p.y - 16 }, { p.x + 22, p.y - 23 }, mine);
        DrawTriangleLines({ p.x, p.y - 30 }, { p.x, p.y - 16 }, { p.x + 22, p.y - 23 },
                          Fade(BLACK, 0.5f));
        DrawCircleV({ p.x, p.y - 32 }, 3, GOLD);
        ui::Text("You", (int)p.x - 12, (int)p.y - 48, 16, RAYWHITE);
    }
    EndMode2D();

    // ---- HUD ----
    DrawRectangle(0, 0, GetScreenWidth(), 34, Fade(BLACK, 0.6f));
    ui::Text(TextFormat("Day %d   Gold: %d   Party: %d", gs.day, gs.gold,
                        gs.player.totalTroops()), 10, 8, 20, RAYWHITE);

    // Time state, top-right: the world is frozen until you move or wait.
    const char* clock = gs.timeFlowing ? "TIME FLOWING" : "TIME PAUSED  (move, or hold SPACE)";
    ui::Text(clock, GetScreenWidth() - ui::Measure(clock, 20) - 12, 8, 20,
             gs.timeFlowing ? LIME : Fade(GOLD, 0.9f));

    if (!gs.resultText.empty())
        ui::Text(gs.resultText.c_str(), 10, 42, 20, GOLD);

    // Prompt to join a nearby clash.
    if (nearSkirmish >= 0 && gs.player.totalTroops() > 0) {
        const Skirmish& sk = gs.skirmishes[nearSkirmish];
        const FactionDef& fa = c.factions[gs.parties[sk.a].faction];
        const FactionDef& fb = c.factions[gs.parties[sk.b].faction];
        const int by = GetScreenHeight() - 92;
        DrawRectangle(0, by - 8, GetScreenWidth(), 52, Fade(BLACK, 0.7f));
        ui::Text("Battle nearby! Join a side:", 10, by, 20, GOLD);
        ui::Text(TextFormat("[1] Aid %s (%d)", fa.name.c_str(), gs.parties[sk.a].totalTroops()),
                 260, by, 20, fa.color);
        ui::Text(TextFormat("[2] Aid %s (%d)", fb.name.c_str(), gs.parties[sk.b].totalTroops()),
                 470, by, 20, fb.color);
    }

    ui::Text("WASD / hold LMB to travel (time flows). Hold SPACE to wait. Click a settlement to enter.",
             10, GetScreenHeight() - 22, 16, Fade(RAYWHITE, 0.7f));

    if (gs.player.totalTroops() == 0)
        ui::Text("Your warband is destroyed... press R to restart.", 10, 70, 20, RED);

    EndDrawing();
}

// ---------------------------------------------------------------------------
// Settlement menu: the overworld is frozen (main.cpp routes here instead of
// CampaignUpdateDraw) while the player is inside a town/castle/village.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Tiled inventory (roadmap D1): Diablo/PoE-style grid. Items are content
// handles with a tileW×tileH footprint. Helpers below are the single source
// of truth for footprints and collision.
// ---------------------------------------------------------------------------

static void ItemFootprint(const Content& c, const InvItem& it, int& w, int& h) {
    if (it.isWeapon) { w = c.weapons[it.handle].tileW; h = c.weapons[it.handle].tileH; }
    else             { w = c.armor[it.handle].tileW;   h = c.armor[it.handle].tileH; }
}

static bool ItemCovers(const Content& c, const InvItem& it, int cx, int cy) {
    int w, h;
    ItemFootprint(c, it, w, h);
    return cx >= it.x && cx < it.x + w && cy >= it.y && cy < it.y + h;
}

// Would item `it` (with top-left at x,y) fit without overlap? `ignore` is an
// index into gs.inventory to skip (the item being moved).
static bool FitsAt(const GameState& gs, const InvItem& it, int x, int y, int ignore) {
    const Content& c = gs.content;
    int w, h;
    ItemFootprint(c, it, w, h);
    if (x < 0 || y < 0 || x + w > INV_W || y + h > INV_H) return false;
    for (int i = 0; i < (int)gs.inventory.size(); ++i) {
        if (i == ignore) continue;
        const InvItem& o = gs.inventory[i];
        int ow, oh;
        ItemFootprint(c, o, ow, oh);
        if (x < o.x + ow && o.x < x + w && y < o.y + oh && o.y < y + h) return false;
    }
    return true;
}

// First free spot scanning row-major; false if the bag is full for this item.
static bool AutoPlace(GameState& gs, InvItem it) {
    for (int y = 0; y < INV_H; ++y)
        for (int x = 0; x < INV_W; ++x) {
            it.x = x; it.y = y;
            if (FitsAt(gs, it, x, y, -1)) { gs.inventory.push_back(it); return true; }
        }
    return false;
}

// Item index covering a cell, or -1.
static int ItemAtCell(const GameState& gs, int cx, int cy) {
    for (int i = 0; i < (int)gs.inventory.size(); ++i)
        if (ItemCovers(gs.content, gs.inventory[i], cx, cy)) return i;
    return -1;
}

void InventoryUpdate(GameState& gs, const CampaignInput& in) {
    const Content& c = gs.content;

    if (in.invPick && in.invCellX >= 0) {
        if (gs.invCarry < 0) {
            gs.invCarry = ItemAtCell(gs, in.invCellX, in.invCellY);   // pick up
        } else if (gs.invCarry < (int)gs.inventory.size()) {
            InvItem& it = gs.inventory[gs.invCarry];                  // put down
            if (FitsAt(gs, it, in.invCellX, in.invCellY, gs.invCarry)) {
                it.x = in.invCellX;
                it.y = in.invCellY;
                gs.invCarry = -1;
            }
        }
    }

    if (in.invEquip && in.invCellX >= 0 && gs.invCarry < 0) {
        const int idx = ItemAtCell(gs, in.invCellX, in.invCellY);
        if (idx >= 0) {
            const InvItem it = gs.inventory[idx];
            Loadout& lo = gs.playerHero.loadout;
            if (it.isWeapon) {
                // Swap with the ACTIVE weapon in the arsenal.
                const int active = lo.get(EquipSlot::Weapon);
                gs.inventory.erase(gs.inventory.begin() + idx);
                for (int w = 0; w < (int)lo.weapons.size(); ++w)
                    if (lo.weapons[w] == active) { lo.weapons[w] = it.handle; break; }
                if (lo.weapons.empty()) lo.weapons.push_back(it.handle);
                lo.set(EquipSlot::Weapon, it.handle);
                if (c.weapons.valid(active)) {
                    InvItem old; old.isWeapon = true; old.handle = active;
                    AutoPlace(gs, old);   // bag full = the old weapon is dropped
                }
            } else {
                const EquipSlot slot = c.armor[it.handle].slot;
                const int old = lo.get(slot);
                gs.inventory.erase(gs.inventory.begin() + idx);
                lo.set(slot, it.handle);
                if (c.armor.valid(old)) {
                    InvItem oldIt; oldIt.isWeapon = false; oldIt.handle = old;
                    AutoPlace(gs, oldIt);
                }
            }
        }
    }

    if (in.leaveSettlement) {
        gs.invCarry = -1;
        gs.screen = Screen::Campaign;
    }
}

void InventoryDraw(const GameState& gs) {
    const Content& c = gs.content;
    BeginDrawing();
    ClearBackground(Color{ 24, 26, 30, 255 });

    const int ox = InvOriginX(), oy = InvOriginY();
    ui::Title("INVENTORY", ox, 60, 44, GOLD);
    ui::Text("LMB pick up / place   E equip   Esc / I back", ox, 116, 20,
             Fade(RAYWHITE, 0.7f));

    // Hero equipment summary.
    const Loadout& lo = gs.playerHero.loadout;
    int ey = 150;
    std::string worn = "Worn: ";
    for (int s = 0; s < EQUIP_SLOT_COUNT; ++s) {
        if (s == (int)EquipSlot::Weapon) continue;
        const int h = lo.slots[s];
        if (c.armor.valid(h)) { worn += c.armor[h].name; worn += "  "; }
    }
    const int wh = lo.get(EquipSlot::Weapon);
    worn += "|  Wielding: ";
    worn += c.weapons.valid(wh) ? c.weapons[wh].name : "nothing";
    ui::Text(worn.c_str(), ox, ey, 18, RAYWHITE);

    // Grid.
    for (int y = 0; y <= INV_H; ++y)
        DrawLine(ox, oy + y * INV_CELL, ox + INV_W * INV_CELL, oy + y * INV_CELL,
                 Fade(RAYWHITE, 0.15f));
    for (int x = 0; x <= INV_W; ++x)
        DrawLine(ox + x * INV_CELL, oy, ox + x * INV_CELL, oy + INV_H * INV_CELL,
                 Fade(RAYWHITE, 0.15f));

    // Items.
    for (int i = 0; i < (int)gs.inventory.size(); ++i) {
        const InvItem& it = gs.inventory[i];
        int w, h;
        ItemFootprint(c, it, w, h);
        const Color tint = it.isWeapon ? c.weapons[it.handle].tint : c.armor[it.handle].tint;
        const char* nm   = it.isWeapon ? c.weapons[it.handle].name.c_str()
                                       : c.armor[it.handle].name.c_str();
        Rectangle r = { (float)(ox + it.x * INV_CELL + 3), (float)(oy + it.y * INV_CELL + 3),
                        (float)(w * INV_CELL - 6), (float)(h * INV_CELL - 6) };
        const bool carried = (i == gs.invCarry);
        DrawRectangleRec(r, Fade(tint, carried ? 0.35f : 0.8f));
        DrawRectangleLinesEx(r, 2, carried ? GOLD : Fade(BLACK, 0.6f));
        ui::Text(TextFormat("%.10s", nm), (int)r.x + 4, (int)r.y + 4, 16, BLACK);
    }
    if (gs.inventory.empty())
        ui::Text("Empty. Win battles to gather loot.", ox, oy + INV_H * INV_CELL + 16,
                 20, Fade(RAYWHITE, 0.6f));

    EndDrawing();
}

// ---------------------------------------------------------------------------
// Party management (roadmap D2): roster review + spending veterancy (C2).
// Rows are troop types the player owns, in troop-registry order.
// ---------------------------------------------------------------------------

// Troop handles the player currently fields, in display order.
static std::vector<int> PartyRows(const GameState& gs) {
    std::vector<int> rows;
    for (int t = 0; t < (int)gs.player.troopCounts.size(); ++t)
        if (gs.player.troopCounts[t] > 0) rows.push_back(t);
    return rows;
}

void PartyUpdate(GameState& gs, const CampaignInput& in) {
    const Content& c = gs.content;
    if ((int)gs.troopXp.size() < c.troops.size())
        gs.troopXp.assign(c.troops.size(), 0);

    const std::vector<int> rows = PartyRows(gs);
    if (in.upgradeSlot >= 0 && in.upgradeSlot < (int)rows.size()) {
        const int t  = rows[in.upgradeSlot];
        const TroopDef& td = c.troops[t];
        if (td.upgradesTo >= 0 && gs.player.troopCounts[t] > 0 &&
            gs.troopXp[t] >= td.upgradeXp) {
            gs.troopXp[t] -= td.upgradeXp;
            gs.player.troopCounts[t]--;
            gs.player.troopCounts[td.upgradesTo]++;
        }
    }
    if (in.dismissSlot >= 0 && in.dismissSlot < (int)rows.size()) {
        const int t = rows[in.dismissSlot];
        if (gs.player.troopCounts[t] > 0) gs.player.troopCounts[t]--;   // off the books
    }
    if (in.leaveSettlement) gs.screen = Screen::Campaign;
}

void PartyDraw(const GameState& gs) {
    const Content& c = gs.content;
    const std::vector<int> rows = PartyRows(gs);

    BeginDrawing();
    ClearBackground(Color{ 24, 26, 30, 255 });
    const int panelX = GetScreenWidth() / 2 - 360;

    // Daily ledger preview so roster decisions are financial decisions.
    int wages = 0;
    for (int t = 0; t < (int)gs.player.troopCounts.size(); ++t)
        wages += gs.player.troopCounts[t] * c.troops[t].wage;
    int income = 0;
    for (const Town& tw : gs.towns)
        if (tw.owner == c.playerFaction) income += SettlementIncome(tw.type);

    ui::Title("YOUR WARBAND", panelX, 60, 44, GOLD);
    ui::Text(TextFormat("Gold: %d    Troops: %d    Daily: +%d income  -%d wages  (%+d)",
                        gs.gold, gs.player.totalTroops(), income, wages, income - wages),
             panelX, 120, 22, income >= wages ? RAYWHITE : Fade(RED, 0.9f));
    ui::Text("Survivors of won battles earn experience; spend it to promote them.",
             panelX, 150, 18, Fade(RAYWHITE, 0.7f));

    int y = 200;
    for (int slot = 0; slot < (int)rows.size(); ++slot) {
        const int t = rows[slot];
        const TroopDef& td = c.troops[t];
        const int xp = (t < (int)gs.troopXp.size()) ? gs.troopXp[t] : 0;
        ui::Text(TextFormat("[%d]  %-10s x%-3d   XP %d", slot + 1, td.name.c_str(),
                            gs.player.troopCounts[t], xp),
                 panelX, y, 24, RAYWHITE);
        if (td.upgradesTo >= 0) {
            const bool can = xp >= td.upgradeXp;
            ui::Text(TextFormat("-> %s  (%d XP)", c.troops[td.upgradesTo].name.c_str(),
                                td.upgradeXp),
                     panelX + 420, y, 20, can ? LIME : Fade(RAYWHITE, 0.45f));
        } else {
            ui::Text("(elite)", panelX + 420, y, 20, Fade(GOLD, 0.6f));
        }
        y += 34;
    }
    if (rows.empty())
        ui::Text("Your warband is empty. Recruit in a friendly settlement.",
                 panelX, y, 22, Fade(RED, 0.8f));

    ui::Text("[1-9] promote one unit    [Shift+1-9] dismiss one    [Esc / P] back",
             panelX, GetScreenHeight() - 48, 20, Fade(RAYWHITE, 0.7f));
    EndDrawing();
}

// ---------------------------------------------------------------------------
// Title screen: New Game / Continue (autosave) / Quit. `TitleUpdate` returns
// false when the player chose Quit.
// ---------------------------------------------------------------------------

// Victory screen: the campaign is won. Any menu choice returns to the title
// (windowed) — a fresh world awaits.
bool VictoryUpdate(GameState& gs, const CampaignInput& in) {
    if (in.menuChoice != 0 || in.leaveSettlement) {
        Content saved = std::move(gs.content);
        gs = GameState{};
        gs.content = std::move(saved);
        CampaignInit(gs);
        gs.screen = Screen::Title;
    }
    return true;
}

void VictoryDraw(const GameState& gs) {
    BeginDrawing();
    ClearBackground(Color{ 16, 18, 22, 255 });
    const int w = GetScreenWidth();
    const char* t1 = "THE LAND IS YOURS";
    ui::Title(t1, (w - ui::MeasureTitle(t1, 72)) / 2, 180, 72, GOLD);
    const char* t2 = TextFormat("Every settlement flies your banner.  Won on day %d, at level %d.",
                                gs.day, gs.playerHero.level);
    ui::Text(t2, (w - ui::Measure(t2, 24)) / 2, 300, 24, RAYWHITE);
    const char* t3 = "[Esc]  Return to the title";
    ui::Text(t3, (w - ui::Measure(t3, 24)) / 2, 420, 24, Fade(RAYWHITE, 0.8f));
    EndDrawing();
}

bool TitleUpdate(GameState& gs, const CampaignInput& in) {
    switch (in.menuChoice) {
        case 1:   // fresh world
            gs.screen = Screen::Campaign;
            break;
        case 2:   // continue from the autosave, if there is one
            if (LoadGame(gs, AutoSavePath())) gs.resultText = "Welcome back.";
            gs.screen = Screen::Campaign;   // load failure = new game
            break;
        case 3:
            return false;
        default: break;
    }
    return true;
}

void TitleDraw(const GameState& gs) {
    (void)gs;
    BeginDrawing();
    ClearBackground(Color{ 20, 22, 26, 255 });

    const int w = GetScreenWidth();
    const int hgt = GetScreenHeight();

    // A dusk field of war: gradient sky, low sun, hill lines, castle silhouette.
    DrawRectangleGradientV(0, 0, w, hgt, Color{ 30, 26, 44, 255 }, Color{ 158, 74, 44, 255 });
    DrawCircleGradient(w / 5, hgt * 2 / 3, 110, Fade(Color{ 255, 180, 90, 255 }, 0.85f),
                       Fade(WHITE, 0.0f));
    DrawRectangle(0, hgt - 160, w, 160, Color{ 26, 20, 24, 255 });          // near ridge
    for (int x = -40; x < w; x += 120)                                       // far hills
        DrawTriangle({ (float)x, (float)(hgt - 150) }, { (float)(x + 140), (float)(hgt - 150) },
                     { (float)(x + 70), (float)(hgt - 230) }, Color{ 38, 30, 36, 255 });
    // castle on the ridge, black against the dusk
    const int cx = w * 3 / 4;
    DrawRectangle(cx - 90, hgt - 300, 180, 150, Color{ 16, 12, 16, 255 });
    for (int b = -90; b < 90; b += 30)
        DrawRectangle(cx + b, hgt - 316, 16, 16, Color{ 16, 12, 16, 255 });
    DrawRectangle(cx - 24, hgt - 380, 48, 90, Color{ 16, 12, 16, 255 });     // keep tower
    DrawLineEx({ (float)cx, (float)(hgt - 380) }, { (float)cx, (float)(hgt - 412) }, 3, Color{ 16, 12, 16, 255 });
    DrawTriangle({ (float)cx, (float)(hgt - 412) }, { (float)cx, (float)(hgt - 396) },
                 { (float)(cx + 26), (float)(hgt - 404) }, Color{ 150, 30, 30, 255 });   // banner
    DrawRectangleGradientV(0, 0, w, hgt / 2, Fade(BLACK, 0.35f), Fade(BLACK, 0.0f));     // vignette for text
    const char* title = "OPENWARBAND";
    ui::Title(title, (w - ui::MeasureTitle(title, 84)) / 2, 150, 84, GOLD);
    const char* sub = "raise a warband - take the land - hold it";
    ui::Text(sub, (w - ui::Measure(sub, 22)) / 2, 260, 22, Fade(RAYWHITE, 0.7f));

    const bool haveSave = FileExists(AutoSavePath());
    int y = 380;
    auto option = [&](const char* txt, Color col) {
        ui::Text(txt, (w - ui::Measure(txt, 30)) / 2, y, 30, col);
        y += 52;
    };
    option("[N]  New Game", RAYWHITE);
    option("[C]  Continue", haveSave ? RAYWHITE : Fade(RAYWHITE, 0.35f));
    option("[Esc]  Quit", Fade(RAYWHITE, 0.8f));

    EndDrawing();
}

// ---------------------------------------------------------------------------
// Character sheet (roadmap D3): level, XP, and attribute points. Attributes
// are pure structure until the balance pass — the sheet says so.
// ---------------------------------------------------------------------------

void CharacterUpdate(GameState& gs, const CampaignInput& in) {
    Character& hero = gs.playerHero;
    if ((int)hero.attributes.size() < gs.content.attributes.size())
        hero.attributes.assign(gs.content.attributes.size(), 0);

    if (in.spendAttr >= 0 && in.spendAttr < gs.content.attributes.size() &&
        hero.attrPoints > 0) {
        hero.attributes[in.spendAttr]++;
        hero.attrPoints--;
    }
    if (in.leaveSettlement) gs.screen = Screen::Campaign;
}

void CharacterDraw(const GameState& gs) {
    const Content& c = gs.content;
    const Character& hero = gs.playerHero;

    BeginDrawing();
    ClearBackground(Color{ 24, 26, 30, 255 });
    const int panelX = GetScreenWidth() / 2 - 360;

    ui::Title("CHARACTER", panelX, 60, 44, GOLD);
    ui::Text(TextFormat("Level %d    XP %d / %d    Points to spend: %d",
                        hero.level, hero.xp,
                        hero.level * 100,   // mirrors HeroXpToLevel; TODO(balance)
                        hero.attrPoints),
             panelX, 122, 22, RAYWHITE);

    int y = 180;
    for (int a = 0; a < c.attributes.size(); ++a) {
        const AttributeDef& ad = c.attributes[a];
        const int v = a < (int)hero.attributes.size() ? hero.attributes[a] : 0;
        ui::Text(TextFormat("[%d]  %-14s %d", a + 1, ad.name.c_str(), v),
                 panelX, y, 26, hero.attrPoints > 0 ? LIME : RAYWHITE);
        ui::Text(ad.hook.c_str(), panelX + 320, y + 4, 16, Fade(RAYWHITE, 0.55f));
        y += 40;
    }

    ui::Text("Attribute effects arrive with the balancing pass - spend freely.",
             panelX, y + 10, 18, Fade(GOLD, 0.7f));
    ui::Text("[1-4] spend a point    [Esc / C] back to the map",
             panelX, GetScreenHeight() - 48, 20, Fade(RAYWHITE, 0.7f));
    EndDrawing();
}

