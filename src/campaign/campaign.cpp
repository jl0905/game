#include "campaign.h"
#include "../settings.h"
#include "../gfx.h"
#include "../battle/character.h"   // roster parade preview (read-only reuse)
#include "../save.h"
#include "../sfx.h"
#include "../town/town.h"   // the gate menu's state + shared layout (U4)
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

// Runtime copy of Content::map.size (the map is data-driven; see assets/map.cfg).
// Set once per world in CampaignInit; a file-local so every helper in this
// module reads the live bounds without threading GameState through.
float MAP_SIZE = 2000.0f;
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

// Nearest settlement owned by `faction` other than `exclude`, or -1.
int NearestOwnedTown(const GameState& gs, int faction, Vector2 from, int exclude) {
    int best = -1;
    float bestD = 1e9f;
    for (int t = 0; t < (int)gs.towns.size(); ++t) {
        if (t == exclude || gs.towns[t].owner != faction) continue;
        const float d = Vector2Distance(from, gs.towns[t].pos);
        if (d < bestD) { bestD = d; best = t; }
    }
    return best;
}

// A caravan (E3): a lightly-guarded trade convoy plying its faction's towns.
Party MakeCaravan(const Content& c, int faction, Vector2 pos, int toTown) {
    Party p;
    p.faction = faction;
    p.pos = p.wanderTarget = pos;
    p.caravan = true;
    p.caravanTo = toTown;
    p.troopCounts.assign(c.troops.size(), 0);
    const std::vector<int>& roster = c.factions[faction].roster;
    if (!roster.empty()) p.troopCounts[roster[0]] = 3;   // TODO(balance): guards
    return p;
}

// How many units of freight a caravan's wagons hold. TODO(balance).
constexpr int CARAVAN_CARGO = 8;

// Fill a caravan's wagons from its stop's market: it buys what the town has
// most of, so surplus flows toward scarcity. Leaves the last unit of each
// ware on the shelf for the player.
void LoadCaravanCargo(const Content& c, Town& from, Party& p) {
    if ((int)p.cargo.size() < c.goods.size()) p.cargo.assign(c.goods.size(), 0);
    if ((int)from.stock.size() < c.goods.size()) return;
    int held = 0;
    for (int q : p.cargo) held += q;
    while (held < CARAVAN_CARGO) {
        // What the town *makes* (cheap source goods, offset < 100) is the
        // true surplus — that's what pays at the far end. Fall back to the
        // fullest shelf if the town produces nothing. Never take the last
        // unit of a ware.
        int best = -1, bestStock = 1;
        for (int g = 0; g < c.goods.size(); ++g) {
            const bool makes = g < (int)from.priceOffset.size() &&
                               from.priceOffset[g] < 100;
            if (makes && from.stock[g] > bestStock) {
                bestStock = from.stock[g];
                best = g;
            }
        }
        if (best < 0)
            for (int g = 0; g < c.goods.size(); ++g)
                if (from.stock[g] > bestStock) { bestStock = from.stock[g]; best = g; }
        if (best < 0) break;
        p.cargoCost += MarketBuyPrice(c, from, best);   // the load's book value (M4)
        from.stock[best]--;
        p.cargo[best]++;
        held++;
    }
}

// Landless traders (the player's convoys, the travelling small folk, M6)
// call at any market at peace with them; a crown's caravans ply its own.
bool TradesAnywhere(const GameState& gs, int faction) {
    if (faction == gs.content.playerFaction) return true;
    for (const Town& t : gs.towns)
        if (t.owner == faction) return false;
    return true;
}

// Where a convoy trades next (M4).
int NearestTradeStop(const GameState& gs, int faction, Vector2 pos, int avoid) {
    if (!TradesAnywhere(gs, faction))
        return NearestOwnedTown(gs, faction, pos, avoid);
    int best = -1;
    float bestD = 1e9f;
    for (int t = 0; t < (int)gs.towns.size(); ++t) {
        if (t == avoid || AtWar(gs, faction, gs.towns[t].owner)) continue;
        const float d = Vector2Distance(pos, gs.towns[t].pos);
        if (d < bestD) { bestD = d; best = t; }
    }
    return best;
}

Vector2 RandomEdgePos() {
    return { Frand(150, MAP_SIZE - 150), Frand(150, MAP_SIZE - 150) };
}

// Radius (in world units) within which a click counts as selecting a town.
constexpr float TOWN_CLICK_RADIUS = 36.0f;

// Shared Gather/Draw layout (K7): the mouse hit-boxes in Gather*Input and the
// row layouts in the draw functions both quote these — never mirrored
// literals, so they cannot drift apart.
namespace layout {
constexpr int SETTINGS_Y = 200, SETTINGS_ROW_H = 44, SETTINGS_ROWS = 5;
constexpr int MARKET_Y   = 230, MARKET_ROW_H   = 32;
constexpr int MARKET_X0  = 120, MARKET_X1      = 700;
constexpr int TITLE_Y    = 380, TITLE_ROW_H    = 52, TITLE_ROWS = 4;
constexpr int PARTY_Y    = 200, PARTY_ROW_H    = 34, PARTY_SLOTS = 9;
constexpr int CHAR_Y     = 180, CHAR_ROW_H     = 40;
constexpr int PANEL_HALF = 360, PANEL_W        = 560;
}   // namespace layout

// Hover highlight (K7): a soft band behind the clickable row under the
// cursor. Draw-only affordance — simulation and the harness never see it.
void DrawHoverRow(int x, int y, int w, int h) {
    const Vector2 m = GetMousePosition();
    if (m.x >= x && m.x < x + w && m.y >= y && m.y < y + h)
        DrawRectangle(x, y, w, h, Fade(GOLD, 0.13f));
}

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
    if (AtWar(gs, e.faction, c.playerFaction) && gs.player.totalTroops() > 0 &&
        !beneathNotice(gs.player.totalTroops())) {
        const float d = Vector2Distance(e.pos, gs.player.pos);
        if (d < best.dist) best = { -1, gs.player.pos, gs.player.totalTroops(), d };
    }
    for (int j = 0; j < (int)gs.parties.size(); ++j) {
        if (j == ei) continue;
        const Party& o = gs.parties[j];
        if (!o.alive || o.engaged) continue;
        if (!AtWar(gs, e.faction, o.faction)) continue;
        if (beneathNotice(o.totalTroops())) continue;
        float d = Vector2Distance(e.pos, o.pos);
        // Outlaws smell freight (L6): a laden caravan reads as half its true
        // distance, so bandits prey on the trade flow the way the player
        // can — and merchants have reason to fear the wild roads.
        if (o.caravan && e.lord.empty() &&
            c.factions[e.faction].behavior == PartyBehavior::Aggressive) {
            int freight = 0;
            for (int q : o.cargo) freight += q;
            if (freight > 0) d *= 0.5f;   // TODO(balance)
        }
        if (d < best.dist) best = { j, o.pos, o.totalTroops(), d };
    }
    return best;
}

// Where party `e` steers this tick, given the foe it has spotted (if any).
// Also records what the party is *doing* in `e.state` for the map and harness.
Vector2 SteerTarget(Party& e, PartyBehavior behavior, const Foe& foe, float sim) {
    if (foe.index != -2) {
        const bool couldWin = e.totalTroops() >= foe.strength / 2;
        switch (behavior) {
            case PartyBehavior::Aggressive:
                if (foe.dist < 500) { e.state = PartyState::Pursuing; return foe.pos; }
                break;
            case PartyBehavior::Patrol:
                if (foe.dist < 300 && couldWin) {                    // opportunistic
                    e.state = PartyState::Pursuing;
                    return foe.pos;
                }
                break;
            case PartyBehavior::Passive:
                if (foe.dist < 220) {                               // flees
                    Vector2 away = Vector2Subtract(e.pos, foe.pos);
                    if (Vector2Length(away) > 1) {
                        e.state = PartyState::Fleeing;
                        return Vector2Add(e.pos, Vector2Scale(Vector2Normalize(away), 200));
                    }
                }
                break;
        }
    }
    e.thinkTimer -= sim;
    if (e.thinkTimer <= 0) {
        e.wanderTarget = { Frand(100, MAP_SIZE - 100), Frand(100, MAP_SIZE - 100) };
        e.thinkTimer = Frand(3, 8);
    }
    e.state = PartyState::Patrolling;
    return e.wanderTarget;
}

// --- fatigue & rest ---------------------------------------------------------
// Marching tires a host. Past FATIGUE_LIMIT it breaks off whatever it was doing,
// rides to the nearest settlement its faction is not at war with, and camps
// there until rested. All flat, TODO(balance).
constexpr float FATIGUE_PER_SEC  = 1.0f;
constexpr float FATIGUE_LIMIT    = 45.0f;
constexpr float REST_RECOVERY    = 6.0f;   // rests off ~7.5× faster than it tires
constexpr float REST_REACH       = 30.0f;  // close enough to make camp

// Steer a tired party home. Returns true (and fills `target`) while the party is
// committed to resting, in which case no other objective applies this tick.
bool UpdateRest(const GameState& gs, Party& e, float sim, Vector2& target) {
    if (e.restTown < 0) {
        e.fatigue += sim * FATIGUE_PER_SEC;
        // A mauled lord counts as spent whatever the clock says: he rides
        // home and stays until fresh volunteers bring him back over half
        // strength (the daily recruit tick refills him there).
        const bool mauled =
            !e.lord.empty() && e.faction >= 0 &&
            e.faction < gs.content.factions.size() &&
            e.totalTroops() < gs.content.factions[e.faction].lordPartySize / 2;
        if (mauled) e.fatigue = FATIGUE_LIMIT;
        if (e.fatigue < FATIGUE_LIMIT) return false;
        float best = 1e9f;
        for (int ti = 0; ti < (int)gs.towns.size(); ++ti) {
            if (AtWar(gs, e.faction, gs.towns[ti].owner)) continue;
            // Lords head for their own banner's settlements — that is where
            // fresh volunteers are (the daily recruit tick).
            if (!e.lord.empty() && gs.towns[ti].owner != e.faction) continue;
            const float d = Vector2Distance(e.pos, gs.towns[ti].pos);
            if (d < best) { best = d; e.restTown = ti; }
        }
        if (e.restTown < 0) { e.fatigue = 0; return false; }   // nowhere to go home to
    }

    const Town& home = gs.towns[e.restTown];
    if (Vector2Distance(e.pos, home.pos) > REST_REACH) {
        e.state = PartyState::Travelling;
        target  = home.pos;
        return true;
    }
    e.state   = PartyState::Resting;
    e.fatigue -= sim * REST_RECOVERY;
    if (e.fatigue <= 0) { e.fatigue = 0; e.restTown = -1; }
    target = e.pos;
    return true;
}

// Remove up to `count` troops from a party, spread across its troop types.
// Named companions never drift, starve, or desert this way (Q3) — heroes
// leave by choice (P3) or not at all.
void RemoveTroops(const Content& c, Party& p, int count) {
    int strippable = 0;
    for (int t = 0; t < (int)p.troopCounts.size(); ++t)
        if (!(t < c.troops.size() && c.troops[t].companion))
            strippable += p.troopCounts[t];
    for (int guard = 0; guard < 10000 && count > 0 && strippable > 0; ++guard) {
        const int t = GetRandomValue(0, (int)p.troopCounts.size() - 1);
        if (t < c.troops.size() && c.troops[t].companion) continue;
        if (p.troopCounts[t] > 0) { p.troopCounts[t]--; count--; strippable--; }
    }
}

// --- live diplomacy (C4): war weariness, truces, rekindled wars -------------
constexpr int   WAR_WEARINESS = 40;    // TODO(balance): casualties that end a war
constexpr float TRUCE_DAYS    = 4.0f;  // TODO(balance): days a sworn truce holds

// Kingdoms treat; outlaw rabble never does (FactionDef::kingdom).
bool IsKingdom(const Content& c, int f) {
    return f >= 0 && f < c.factions.size() && c.factions[f].kingdom;
}

// Feed a war's butcher's bill; enough of it and the two kingdoms swear peace.
void AddWarScore(GameState& gs, int a, int b, int casualties) {
    const Content& c = gs.content;
    const int n = c.factions.size();
    if (a < 0 || b < 0 || a == b || casualties <= 0) return;
    if ((int)gs.hostile.size() != (size_t)n * n) return;
    if (!IsKingdom(c, a) || !IsKingdom(c, b)) return;
    const size_t ij = (size_t)a * n + b, ji = (size_t)b * n + a;
    if (!gs.hostile[ij]) return;
    gs.warScore[ij] += casualties;
    gs.warScore[ji] = gs.warScore[ij];
    if (gs.warScore[ij] >= WAR_WEARINESS) {
        gs.hostile[ij] = gs.hostile[ji] = 0;
        gs.warScore[ij] = gs.warScore[ji] = 0;
        gs.truceDays[ij] = gs.truceDays[ji] = TRUCE_DAYS;
        gs.resultText = TextFormat("PEACE:  %s and %s, weary of war, swear a truce.",
                                   c.factions[a].name.c_str(), c.factions[b].name.c_str());
    }
}

// One world day passes: truces run down, and a lapsed truce between kingdoms
// whose base relation is war rekindles the fighting.
void DiplomacyDayTick(GameState& gs) {
    const Content& c = gs.content;
    const int n = c.factions.size();
    if ((int)gs.hostile.size() != (size_t)n * n) return;
    for (int a = 0; a < n; ++a)
        for (int b = a + 1; b < n; ++b) {
            const size_t ij = (size_t)a * n + b, ji = (size_t)b * n + a;
            if (gs.truceDays[ij] <= 0) continue;
            gs.truceDays[ij] -= 1.0f;
            gs.truceDays[ji] = gs.truceDays[ij];
            if (gs.truceDays[ij] <= 0 && AreFactionsHostile(c, a, b)) {
                gs.hostile[ij] = gs.hostile[ji] = 1;
                gs.resultText = TextFormat("WAR:  the truce lapses — %s and %s take up arms again!",
                                           c.factions[a].name.c_str(), c.factions[b].name.c_str());
            }
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

    RemoveTroops(gs.content, winner, GetRandomValue(loserStrength / 2, loserStrength));
    loser.alive = false;
    if (winner.totalTroops() <= 0) winner.alive = false;  // mutual annihilation
    AddWarScore(gs, a.faction, b.faction, loserStrength);
}

// Camera centred on the player; used by input gathering and drawing.
// Map zoom (T2): pure view state, wheel-driven in Gather (device side, so
// headless sims never see it). Clamped so neither pixels nor the whole map
// swallow the screen.
float g_mapZoom = 1.0f;
constexpr float MAP_ZOOM_MIN = 0.35f, MAP_ZOOM_MAX = 2.5f;

Camera2D CampaignCamera(const GameState& gs) {
    Camera2D cam = { 0 };
    cam.target = gs.player.pos;
    cam.offset = { GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f };
    cam.zoom = g_mapZoom;
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
// Garrison sizing (U3, playtest-tuned 2026-07-21): walls are worth manning —
// settlements were falling like dominoes at 8/4/12.
inline int GarrisonCap(SettlementType t) {
    if (t == SettlementType::Village) return 10;
    if (t == SettlementType::Castle)  return 30;
    return 20;   // towns
}

void InstallGarrison(const Content& c, Town& t, Party& attacker) {
    const int size = GarrisonCap(t.type);
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

// Auto-resolve an AI siege: strength-weighted, with the walls fighting for
// the defenders (U3): the garrison counts 1.7x, and a repelled assault
// bleeds the attacker white — armies can die on walls now.
void ResolveAISiege(GameState& gs, AISiege& sg) {
    if (sg.party < 0 || sg.party >= (int)gs.parties.size()) return;
    Party& a = gs.parties[sg.party];
    a.engaged = false;
    if (!a.alive || sg.town < 0 || sg.town >= (int)gs.towns.size()) return;
    Town& t = gs.towns[sg.town];
    if (!AtWar(gs, a.faction, t.owner)) return;  // already flipped, or peace broke out

    const int sa = a.totalTroops();
    const int sd = t.garrisonSize();
    if (sa <= 0) { a.alive = false; return; }
    const int defender = t.owner;

    const float wallStrength = (float)sd * 1.7f;   // U3: walls fight too
    const bool taken = Frand(0, (float)sa + wallStrength) < (float)sa;
    if (taken) {
        RemoveTroops(gs.content, a, GetRandomValue(sd / 2, sd + sd / 2));
        t.owner = a.faction;
        InstallGarrison(gs.content, t, a);
        if (a.totalTroops() <= 0) a.alive = false;
    } else {
        RemoveTroops(gs.content, a, GetRandomValue(sa / 2, sa));   // repelled, bled white
        if (a.totalTroops() <= 0) a.alive = false;
        // the garrison is bloodied too
        int loss = GetRandomValue(0, sd / 2);
        for (int tr = 0; tr < (int)t.garrison.size() && loss > 0; ++tr)
            while (t.garrison[tr] > 0 && loss > 0) { t.garrison[tr]--; loss--; }
    }
    AddWarScore(gs, a.faction, defender, sa - a.totalTroops() + sd - t.garrisonSize());
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

    MAP_SIZE = c.map.size;   // the world takes its bounds from the map def

    // Player party + hero avatar.
    gs.player = Party{};
    gs.player.isPlayer = true;
    gs.player.faction = c.playerFaction;
    gs.player.pos = c.map.playerStart;
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

    // Settlements come from the map definition (assets/map.cfg or the built-in
    // fallback). Owners are resolved from faction ids here, at world creation.
    gs.towns.clear();
    for (const MapDef::TownSpec& spec : c.map.towns) {
        Town t;
        t.pos  = spec.pos;
        t.name = spec.name;
        t.type = spec.type;
        t.owner = spec.owner == "player" ? c.playerFaction
                : spec.owner == "none"   ? -1
                                         : c.factions.find(spec.owner.c_str());
        gs.towns.push_back(t);
    }

    // Garrison every owned settlement from its owner's roster. Relative sizes
    // are settlement identity (a castle holds more than a village);
    // TODO(balance): the actual numbers.
    for (Town& t : gs.towns) {
        t.garrison.assign(c.troops.size(), 0);
        if (t.owner < 0) continue;
        const int size = GarrisonCap(t.type);   // U3: walls worth manning
        const std::vector<int>& roster = c.factions[t.owner].roster;
        for (int i = 0; i < size && !roster.empty(); ++i)
            t.garrison[roster[i % (int)roster.size()]]++;
    }

    // Stock every settlement's market. Price spreads are settlement identity
    // (the E2 trade loop): villages sell their raw produce cheap and pay dear
    // for craftwork; towns are the mirror; castles are indifferent quarters
    // with thin stock. TODO(balance): every number here.
    constexpr int PRICE_AT_SOURCE = 70;    // where the good is made
    constexpr int PRICE_AT_MARKET = 130;   // where it is wanted
    for (Town& t : gs.towns) {
        const bool village = t.type == SettlementType::Village;
        const bool castle  = t.type == SettlementType::Castle;
        t.stock.assign(c.goods.size(), castle ? 5 : 10);
        t.priceOffset.assign(c.goods.size(), 100);
        if (!castle)
            for (int g = 0; g < c.goods.size(); ++g)
                t.priceOffset[g] = (c.goods[g].raw == village) ? PRICE_AT_SOURCE
                                                               : PRICE_AT_MARKET;
    }
    gs.goods.assign(c.goods.size(), 0);
    gs.enterpriseAt.assign(gs.towns.size(), -1);

    // Bandit dens from the map definition (H2).
    gs.lairs.clear();
    for (const MapDef::LairSpec& ls : c.map.lairs) {
        Lair l;
        l.pos = ls.pos;
        l.faction = c.factions.find(ls.faction.c_str());
        if (l.faction >= 0) gs.lairs.push_back(l);
    }

    gs.parties.clear();
    gs.skirmishes.clear();
    gs.aiSieges.clear();
    gs.lordRespawns.clear();
    gs.playerLosses.assign(c.troops.size(), 0);
    gs.troopXp.assign(c.troops.size(), 0);
    gs.prisoners.assign(c.troops.size(), 0);
    const std::vector<int> roamers = RoamingFactions(c);
    for (int i = 0; i < c.map.startingParties; ++i)
        gs.parties.push_back(MakeParty(c, roamers[i % roamers.size()], RandomEdgePos()));

    // Lords muster their hosts at a settlement their faction holds.
    for (int f = 0; f < c.factions.size(); ++f)
        for (const std::string& name : c.factions[f].lords)
            gs.parties.push_back(MakeLordParty(c, f, name, FactionHome(gs, f)));

    gs.relations.assign(c.factions.size(), 0);

    // Live diplomacy starts from the static relations table.
    gs.hostile = c.hostile;
    gs.warScore.assign(gs.hostile.size(), 0);
    gs.truceDays.assign(gs.hostile.size(), 0.0f);
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

// NudgeRelation lives in world.h now (M5): court dialogue moves standing too.
// TODO(balance): every delta at the call sites.

// A ruler's purse (S5): the whole day's flows in one place, quoted
// identically by the daily tick, the party screen, and the kingdom ledger.
// Landless raised lords draw a retainer (landed ones tax their own fiefs,
// M3); manned walls eat pay. Declared in campaign.h. TODO(balance): rates.
DayLedger ComputeLedger(const GameState& gs) {
    const Content& c = gs.content;
    DayLedger L;
    for (const Town& t : gs.towns) {
        if (t.owner != c.playerFaction) continue;
        if (t.fiefLord.empty()) {
            L.income      += SettlementIncome(t.type) * t.prosperity / 100;
            L.garrisonPay += (t.garrisonSize() + 1) / 2;
        }
    }
    for (int ti = 0; ti < (int)gs.towns.size() && ti < (int)gs.enterpriseAt.size(); ++ti)
        if (c.enterprises.valid(gs.enterpriseAt[ti]) &&
            !AtWar(gs, gs.towns[ti].owner, c.playerFaction))
            L.enterprise += c.enterprises[gs.enterpriseAt[ti]].dailyIncome *
                            gs.towns[ti].prosperity / 100;
    for (int t = 0; t < (int)gs.player.troopCounts.size() && t < c.troops.size(); ++t)
        L.wages += gs.player.troopCounts[t] * c.troops[t].wage;
    for (const Party& p : gs.parties) {
        if (!p.alive || p.faction != c.playerFaction || p.lord.empty()) continue;
        bool landed = false;
        for (const Town& t : gs.towns)
            if (t.fiefLord == p.lord) { landed = true; break; }
        if (!landed) L.lordPay += 10;   // a landless lord's retainer
    }
    return L;
}

// Pay out the active quest (F4) and clear it.
static void CompleteQuest(GameState& gs) {
    const QuestDef& qd = gs.content.quests[gs.activeQuest];
    gs.gold += qd.goldReward;
    NudgeRelation(gs, gs.questFaction, qd.relationReward);
    gs.renown += 2;   // honest work builds a name (M1). TODO(balance)
    gs.honor  += 1;
    gs.resultText = TextFormat("QUEST COMPLETE: %s  +%d gold", qd.name.c_str(),
                               qd.goldReward);
    gs.activeQuest = -1;
    gs.questFaction = -1;
    gs.questTown = -1;
    gs.questProgress = 0;
}

static void ApplyBattleResult(GameState& gs) {
    // The aftermath card starts fresh each battle; branches below fill it.
    gs.battleReport.clear();
    gs.battleReportTimer = 7.0f;   // presentational; drawn fading over the map

    // Tournament bout (G2): borrowed fighters fought, so no casualties, loot,
    // captives or war score touch the world — only the purse and the champion's
    // renown. TODO(balance): purse size.
    if (gs.arenaFight) {
        // A won round advances the bracket (K3); the third crowns a champion.
        if (gs.battleWon && gs.arenaRound < 3) {
            gs.arenaRound++;
            gs.screen = Screen::Battle;   // straight into the next round
            gs.resultText = TextFormat("Round %d!  The field narrows.", gs.arenaRound);
            gs.battlePartyIndex = -1;
            gs.battleAllyIndex  = -1;
            return;
        }
        gs.arenaFight = false;
        gs.currentSettlement = -1;   // the bracket spills back onto the map
        if (gs.battleWon) {
            const int purse  = 150;                 // TODO(balance)
            const int payout = purse + gs.arenaBet * 3;   // stake pays 3x
            gs.gold += payout;
            Character& hero = gs.playerHero;
            hero.xp += HERO_XP_PER_WIN;
            while (hero.xp >= HeroXpToLevel(hero.level)) {
                hero.xp -= HeroXpToLevel(hero.level);
                hero.level++;
                hero.attrPoints++;
            }
            gs.resultText = gs.arenaBet > 0
                ? TextFormat("TOURNAMENT CHAMPION!  Purse %d + winnings %d gold.",
                             purse, gs.arenaBet * 3)
                : TextFormat("TOURNAMENT CHAMPION!  The purse is %d gold.", purse);
            gs.battleReport.push_back("TOURNAMENT CHAMPION");
            gs.battleReport.push_back(TextFormat("Winnings: %d gold      Hero: +%d XP",
                                                 payout, HERO_XP_PER_WIN));
            gs.renown += 5;   // the crowd remembers a champion (M1). TODO(balance)
        } else {
            gs.resultText = gs.arenaBet > 0
                ? TextFormat("Cast out in round %d... your %d-gold stake is gone.",
                             gs.arenaRound, gs.arenaBet)
                : TextFormat("Cast out in round %d... better luck next bracket.",
                             gs.arenaRound);
            gs.battleReport.push_back("DEFEATED IN THE RING");
        }
        gs.arenaRound = 0;
        gs.arenaBet   = 0;
        gs.battlePartyIndex = -1;
        gs.battleAllyIndex  = -1;
        gs.allyLosses.clear();
        return;
    }

    // Knocked out, not dead (Q3): named companions are carried senseless
    // from the field — they miss the rest of the fight but never the next.
    for (int t = 0; t < (int)gs.playerLosses.size() &&
                    t < gs.content.troops.size(); ++t)
        if (gs.playerLosses[t] > 0 && gs.content.troops[t].companion) {
            gs.battleReport.push_back(TextFormat(
                "%s is knocked senseless - but lives.",
                gs.content.troops[t].name.c_str()));
            gs.playerLosses[t] = 0;
        }

    // The wounded cart (S3): half of every line's fallen are wounded, not
    // dead — they ride behind the warband and heal. TODO(balance): share.
    {
        if ((int)gs.wounded.size() < gs.content.troops.size())
            gs.wounded.assign(gs.content.troops.size(), 0);
        int hurt = 0;
        for (int t = 0; t < (int)gs.playerLosses.size() &&
                        t < (int)gs.wounded.size(); ++t) {
            const int w = gs.playerLosses[t] / 2;
            gs.playerLosses[t] -= w;
            gs.wounded[t]      += w;
            hurt += w;
        }
        if (hurt > 0)
            gs.battleReport.push_back(TextFormat(
                "Wounded: %d  (they will heal and return)", hurt));
    }

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

        // Renown (M1): word of a victory travels — further when the field
        // was bloody. TODO(balance): the scale.
        int slain = 0;
        for (int v : gs.enemyLosses) slain += v;
        gs.renown += 1 + std::min(4, slain / 5);

        // First victory (P4): tell a new captain what winning is for.
        if (!(gs.hintsSeen & 1)) {
            gs.hintsSeen |= 1;
            gs.battleReport.push_back(
                "Survivors earn experience - press P to promote them.");
        }

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

    // Named lords remember the day (N4). TODO(balance): the deltas.
    if (gs.battleWon && enemy && !enemy->lord.empty()) {
        LordOpinion(gs, enemy->lord) -= 15;   // you broke his host
        // And took him (O2): a captive lord does not ride again until
        // ransomed or released at a settlement (U / Y).
        gs.capturedLords.push_back({ enemy->lord, enemy->faction });
        gs.battleReport.push_back(
            TextFormat("Lord %s is taken prisoner!", enemy->lord.c_str()));
        enemy->lord.clear();   // no respawn while he sits in your train
    }
    if (gs.battleWon && ally && !ally->lord.empty())
        LordOpinion(gs, ally->lord) += 10;    // you bled beside him

    // The butcher's bill of the player's own battles feeds war weariness.
    {
        int enemyFaction = enemy ? enemy->faction : -1;
        if (gs.siegeTownIndex >= 0 && gs.siegeTownIndex < (int)gs.towns.size())
            enemyFaction = gs.towns[gs.siegeTownIndex].owner;
        int bill = 0;
        for (int v : gs.playerLosses) bill += v;
        for (int v : gs.enemyLosses)  bill += v;
        AddWarScore(gs, gs.content.playerFaction, enemyFaction, bill);
    }

    // Siege outcome: the garrison takes its casualties; a captured settlement
    // changes hands (villages are "sacked", walls are "stormed").
    if (gs.siegeTownIndex >= 0 && gs.siegeTownIndex < (int)gs.towns.size()) {
        Town& t = gs.towns[gs.siegeTownIndex];
        for (int i = 0; i < (int)t.garrison.size() && i < (int)gs.enemyLosses.size(); ++i) {
            t.garrison[i] -= gs.enemyLosses[i];
            if (t.garrison[i] < 0) t.garrison[i] = 0;
        }

        // The relief lord's fate (P2): the same lord found at launch. Beaten
        // beside the walls he falls with them — and into your train (O2);
        // victorious, his host stands (the garrison's books took the losses).
        const int relief = ReliefLordFor(gs, gs.siegeTownIndex);
        if (relief >= 0) {
            Party& r = gs.parties[relief];
            if (gs.battleWon) {
                LordOpinion(gs, r.lord) -= 15;
                gs.capturedLords.push_back({ r.lord, r.faction });
                gs.battleReport.push_back(TextFormat(
                    "Lord %s fell with the walls - taken prisoner!", r.lord.c_str()));
                r.lord.clear();
                r.alive = false;
            } else {
                gs.battleReport.push_back(TextFormat(
                    "Lord %s's relief held the day.", r.lord.c_str()));
            }
        }
        if (gs.battleWon && gs.raidingVillage) {
            // A raid (P1): the banner stays, the countryside burns. Gold and
            // wares out, prosperity crashed, a black mark on your name.
            // TODO(balance): all of it.
            const int loot = 100 + GetRandomValue(0, 50);
            gs.gold += loot;
            if ((int)gs.goods.size() < gs.content.goods.size())
                gs.goods.assign(gs.content.goods.size(), 0);
            int carried = 0, taken = 0;
            for (int q : gs.goods) carried += q;
            for (int g = 0; g < (int)t.stock.size() &&
                            g < gs.content.goods.size(); ++g)
                while (t.stock[g] > 0 && carried < GOODS_CAP && taken < 8) {
                    t.stock[g]--; gs.goods[g]++; carried++; taken++;
                }
            t.prosperity = std::max(30, t.prosperity - 50);
            NudgeRelation(gs, t.owner, -15);
            gs.honor  -= 3;
            gs.renown += 1;   // infamy is still fame
            gs.resultText = TextFormat(
                "%s BURNS.  Loot: %d gold, %d wares. Word of it spreads.",
                t.name.c_str(), loot, taken);
            gs.battleReport.push_back(std::string(t.name) + " BURNS");
            // The company has opinions (P3): temperaments speak at the fire.
            for (int tr = 0; tr < gs.content.troops.size(); ++tr) {
                if (!gs.content.troops[tr].companion ||
                    gs.player.troopCounts[tr] <= 0) continue;
                const TroopDef& td = gs.content.troops[tr];
                if (td.temper == "honorable")
                    gs.battleReport.push_back(TextFormat(
                        "%s: \"I wanted no part of this.\"", td.name.c_str()));
                else if (td.temper == "grim")
                    gs.battleReport.push_back(TextFormat(
                        "%s grins: \"Good haul.\"", td.name.c_str()));
            }
        } else if (gs.battleWon) {
            NudgeRelation(gs, t.owner, -20);   // you took their land
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
        if (!(gs.battleWon && gs.raidingVillage))   // raids reported above
            gs.battleReport.push_back(gs.battleWon
                                          ? std::string(t.name) + " IS TAKEN"
                                          : "THE ASSAULT IS REPELLED");
        const std::string slainS = LossSummary(gs.content, gs.enemyLosses);
        if (!slainS.empty())  gs.battleReport.push_back("Garrison slain:  " + slainS);
        if (!fallenS.empty()) gs.battleReport.push_back("Your fallen:  " + fallenS);
        gs.siegeTownIndex   = -1;
        gs.siegeLaunchPrep  = 0;   // the engines burned with the assault (N1)
        gs.raidingVillage   = false;
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
        gs.battleReport.push_back("VICTORY");
        gs.battleReport.push_back(TextFormat("Loot: %d gold      Hero: +%d XP", loot, HERO_XP_PER_WIN));
        if (enemy) NudgeRelation(gs, enemy->faction, enemy->caravan ? -10 : -5);
        if (ally)  NudgeRelation(gs, ally->faction, +10);   // you bled beside them

        // A stormed den burns with its defenders (H2).
        if (gs.lairBattle >= 0 && gs.lairBattle < (int)gs.lairs.size()) {
            gs.lairs[gs.lairBattle].alive = false;
            gs.resultText += "   The den is burned out.";
            gs.battleReport.push_back("The den is burned out for good.");
        }

        // Bandit-hunt quests (F4) count broken outlaw bands.
        if (gs.activeQuest >= 0 && enemy &&
            gs.content.quests[gs.activeQuest].type == QuestType::HuntBandits &&
            !gs.content.factions[enemy->faction].kingdom &&
            ++gs.questProgress >= gs.content.quests[gs.activeQuest].amount)
            CompleteQuest(gs);

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
        if (captives > 0) {
            gs.resultText += TextFormat("   Captives: %d", captives);
            gs.battleReport.push_back(TextFormat("Captives taken: %d  (ransom at a tavern)", captives));
            if (!(gs.hintsSeen & 2)) {   // first captives (P4)
                gs.hintsSeen |= 2;
                gs.battleReport.push_back(
                    "Captives pay: press R in any settlement's tavern.");
            }
        }

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
                gs.battleReport.push_back(TextFormat("Picked up: %s", nm));
                if (!(gs.hintsSeen & 4)) {   // first loot (P4)
                    gs.hintsSeen |= 4;
                    gs.battleReport.push_back(
                        "Loot lands in your bag - press I, then E to wear it "
                        "(Tab fits companions).");
                }
            }
        }

        // Caravan plunder (E3): a beaten convoy spills its wares into the
        // saddlebags, up to capacity. TODO(balance): the haul size.
        if (gs.battlePartyIndex >= 0 && gs.battlePartyIndex < (int)gs.parties.size() &&
            gs.parties[gs.battlePartyIndex].caravan && gs.content.goods.size() > 0) {
            if ((int)gs.goods.size() < gs.content.goods.size())
                gs.goods.assign(gs.content.goods.size(), 0);
            int carried = 0;
            for (int q : gs.goods) carried += q;
            // The convoy spills what it actually carries — plundered wares
            // are real freight pulled out of the world's trade flow.
            Party& convoy = gs.parties[gs.battlePartyIndex];
            int taken = 0;
            for (int g = 0; g < (int)convoy.cargo.size() &&
                            g < gs.content.goods.size(); ++g)
                while (convoy.cargo[g] > 0 && carried < GOODS_CAP) {
                    convoy.cargo[g]--;
                    gs.goods[g]++;
                    carried++; taken++;
                }
            if (taken > 0) {
                gs.resultText += TextFormat("   Plundered %d wares", taken);
                gs.battleReport.push_back(TextFormat("Caravan plundered: %d wares", taken));
                gs.honor -= 1;   // robbing merchants stains a name (M1). TODO(balance)
            }
        }
    } else {
        gs.resultText = "DEFEAT...  You escape with the survivors.";
        gs.battleReport.push_back("DEFEAT");
        gs.battleReport.push_back("You escape with the survivors.");
        gs.player.pos.x = Clamp(gs.player.pos.x + Frand(-300, 300), 100, MAP_SIZE - 100);
        gs.player.pos.y = Clamp(gs.player.pos.y + Frand(-300, 300), 100, MAP_SIZE - 100);
        if (ally) ally->alive = false;   // the side you backed lost the field
    }
    if (!slain.empty())  gs.resultText += "   Slain: " + slain;
    if (!fallen.empty()) gs.resultText += "   Fallen: " + fallen;
    if (!slain.empty())  gs.battleReport.push_back("Enemy slain:  " + slain);
    if (!fallen.empty()) gs.battleReport.push_back("Your fallen:  " + fallen);

    // A party wiped out of troops is gone regardless of who "won".
    if (ally && ally->totalTroops() <= 0) ally->alive = false;

    gs.battlePartyIndex = -1;
    gs.battleAllyIndex  = -1;
    gs.lairBattle       = -1;
    gs.allyLosses.clear();
}

// Read the real devices into campaign intent. Windowed play only — the
// headless harness builds CampaignInput directly.
CampaignInput GatherCampaignInput(const GameState& gs) {
    CampaignInput in;

    if (gs.screen == Screen::Settlement) {
        // The gate menu (U4): number keys and clicks pick rows; walking
        // hands the same keys back to recruits and hotkeys.
        if (TownInMenu()) {
            for (int r = 0; r < 9; ++r)
                if (IsKeyPressed(KEY_ONE + r)) in.menuChoice = r + 1;
            if (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_ZERO))
                in.menuChoice = 10;   // visit the settlement
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                const Vector2 m = GetMousePosition();
                const int x0 = GetScreenWidth() / 2 - townmenu::X_HALF;
                const int row = ((int)m.y - townmenu::Y) / townmenu::ROW_H;
                if (m.x >= x0 && m.x < x0 + townmenu::X_HALF * 2 &&
                    m.y >= townmenu::Y && row >= 0 && row < townmenu::ROWS)
                    in.menuChoice = row + 1;
            }
            if (IsKeyPressed(KEY_ESCAPE)) in.leaveSettlement = true;
            return in;
        }
        // Walking a settlement: menu intents only — movement is gathered
        // separately via GatherBattleInput (same third-person controls).
        const std::vector<int>& roster =
            gs.content.factions[gs.content.playerFaction].roster;
        for (int slot = 0; slot < (int)roster.size(); ++slot)
            if (IsKeyPressed(KEY_ONE + slot)) in.recruitSlot = slot;
        in.ransom   = IsKeyPressed(KEY_R);
        in.interact = IsKeyPressed(KEY_E);
        in.openMarket = IsKeyPressed(KEY_M);
        in.tournament = IsKeyPressed(KEY_T);
        in.tournamentBet = in.tournament &&
                           (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
        in.swear      = IsKeyPressed(KEY_V);
        in.quest      = IsKeyPressed(KEY_G);
        in.hire       = IsKeyPressed(KEY_H);
        in.raiseLord  = IsKeyPressed(KEY_L);
        in.ransomLords  = IsKeyPressed(KEY_U);   // prisoner lords (O2)
        in.releaseLords = IsKeyPressed(KEY_Y);
        {   // garrison your walls (S2)
            const bool shiftF = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            in.garrisonOne   = IsKeyPressed(KEY_F) && !shiftF;
            in.ungarrisonOne = IsKeyPressed(KEY_F) && shiftF;
        }
        if (IsKeyPressed(KEY_ESCAPE)) in.leaveSettlement = true;
        return in;
    }

    if (gs.screen == Screen::Settings) {
        for (int row = 0; row < 5; ++row)
            if (IsKeyPressed(KEY_ONE + row)) in.settingsRow = row;
        // Mouse (H3 pattern): rows quote the shared layout (K7).
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            const Vector2 m = GetMousePosition();
            const int row = ((int)m.y - layout::SETTINGS_Y) / layout::SETTINGS_ROW_H;
            if (m.y >= layout::SETTINGS_Y && row >= 0 && row < layout::SETTINGS_ROWS)
                in.settingsRow = row;
        }
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_O)) in.leaveSettlement = true;
        return in;
    }

    if (gs.screen == Screen::Dialogue) {
        if (IsKeyPressed(KEY_ONE))   in.menuChoice = 1;
        if (IsKeyPressed(KEY_TWO))   in.menuChoice = 2;
        if (IsKeyPressed(KEY_THREE)) in.menuChoice = 3;
        if (IsKeyPressed(KEY_FOUR))  in.menuChoice = 4;   // grant a fief (M3)
        if (IsKeyPressed(KEY_FIVE))  in.menuChoice = 5;   // marriage suit (M5)
        if (IsKeyPressed(KEY_SIX))   in.menuChoice = 6;   // rebellion (O6)
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_E)) in.leaveSettlement = true;
        return in;
    }

    if (gs.screen == Screen::Market) {
        const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        for (int slot = 0; slot < 9; ++slot)
            if (IsKeyPressed(KEY_ONE + slot))
                (shift ? in.sellGood : in.buyGood) = slot;
        // Mouse (H3): click a ware row to buy one, right-click to sell one
        // (rows quote the shared layout, K7).
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
            IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            const Vector2 m = GetMousePosition();
            const int row = ((int)m.y - layout::MARKET_Y) / layout::MARKET_ROW_H;
            if (m.x >= layout::MARKET_X0 && m.x < layout::MARKET_X1 &&
                m.y >= layout::MARKET_Y &&
                row >= 0 && row < gs.content.goods.size()) {
                if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || shift)
                    in.sellGood = row;
                else
                    in.buyGood = row;
            }
        }
        in.buyEnterprise = IsKeyPressed(KEY_B);
        in.sendCaravan   = IsKeyPressed(KEY_C);   // outfit a convoy (M4)
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_M)) in.leaveSettlement = true;
        return in;
    }

    if (gs.screen == Screen::Title) {
        if (IsKeyPressed(KEY_N))      in.menuChoice = 1;
        if (IsKeyPressed(KEY_C))      in.menuChoice = 2;
        if (IsKeyPressed(KEY_L))      in.menuChoice = 3;   // load menu (N3)
        if (IsKeyPressed(KEY_ESCAPE)) in.menuChoice = 4;
        in.openSettings = IsKeyPressed(KEY_O);
        // Mouse (H3): option bands quote the shared layout (K7).
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            const Vector2 m = GetMousePosition();
            const int row = ((int)m.y - layout::TITLE_Y) / layout::TITLE_ROW_H;
            if (m.y >= layout::TITLE_Y && row >= 0 && row < layout::TITLE_ROWS)
                in.menuChoice = row + 1;
        }
        return in;
    }

    if (gs.screen == Screen::Kingdom) {
        for (int r = 0; r < 9; ++r)   // sue for peace by war row (S1)
            if (IsKeyPressed(KEY_ONE + r)) in.menuChoice = r + 1;
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_B))
            in.leaveSettlement = true;
        return in;
    }

    if (gs.screen == Screen::LoadMenu) {
        for (int r = 0; r < 4; ++r)
            if (IsKeyPressed(KEY_ONE + r)) in.menuChoice = r + 1;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            const Vector2 m = GetMousePosition();
            const int row = ((int)m.y - layout::TITLE_Y) / layout::TITLE_ROW_H;
            if (m.y >= layout::TITLE_Y && row >= 0 && row < 4)
                in.menuChoice = row + 1;
        }
        if (IsKeyPressed(KEY_ESCAPE)) in.leaveSettlement = true;
        return in;
    }

    if (gs.screen == Screen::Background) {
        if (IsKeyPressed(KEY_ONE))   in.menuChoice = 1;
        if (IsKeyPressed(KEY_TWO))   in.menuChoice = 2;
        if (IsKeyPressed(KEY_THREE)) in.menuChoice = 3;
        // Mouse: option bands mirror BackgroundDraw via the shared layout.
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            const Vector2 m = GetMousePosition();
            const int y0 = layout::TITLE_Y - 100;
            const int rowH = layout::TITLE_ROW_H + 14;
            const int row = ((int)m.y - y0) / rowH;
            if (m.y >= y0 && row >= 0 && row < 3) in.menuChoice = row + 1;
        }
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
        // Mouse (H3): click a roster row to promote, right-click to dismiss.
        // Hit-boxes quote the shared layout (K7).
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
            IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            const Vector2 m = GetMousePosition();
            const int panelX = GetScreenWidth() / 2 - layout::PANEL_HALF;
            const int row = ((int)m.y - layout::PARTY_Y) / layout::PARTY_ROW_H;
            if (m.x >= panelX && m.x < panelX + layout::PANEL_W &&
                m.y >= layout::PARTY_Y &&
                row >= 0 && row < layout::PARTY_SLOTS) {
                if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || shift)
                    in.dismissSlot = row;
                else
                    in.upgradeSlot = row;
            }
        }
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P)) in.leaveSettlement = true;
        return in;
    }

    if (gs.screen == Screen::Character) {
        for (int slot = 0; slot < 9; ++slot)
            if (IsKeyPressed(KEY_ONE + slot)) in.spendAttr = slot;
        // Mouse (H3): click an attribute row to spend a point (rows quote
        // the shared layout, K7).
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            const Vector2 m = GetMousePosition();
            const int panelX = GetScreenWidth() / 2 - layout::PANEL_HALF;
            const int row = ((int)m.y - layout::CHAR_Y) / layout::CHAR_ROW_H;
            if (m.x >= panelX && m.x < panelX + layout::PANEL_W &&
                m.y >= layout::CHAR_Y &&
                row >= 0 && row < gs.content.attributes.size())
                in.spendAttr = row;
        }
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
        in.invCycleTarget = IsKeyPressed(KEY_TAB);
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_I)) in.leaveSettlement = true;
        return in;
    }

    // Map zoom (T2): the wheel rides the map in and out.
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f)
        g_mapZoom = Clamp(g_mapZoom * (1.0f + wheel * 0.12f),
                          MAP_ZOOM_MIN, MAP_ZOOM_MAX);

    const Camera2D cam = CampaignCamera(gs);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        const Vector2 world = GetScreenToWorld2D(GetMousePosition(), cam);
        for (int i = 0; i < (int)gs.towns.size(); ++i)
            if (Vector2Distance(world, gs.towns[i].pos) < TOWN_CLICK_RADIUS)
                in.clickSettlement = i;
        for (int i = 0; i < (int)gs.lairs.size(); ++i)
            if (gs.lairs[i].alive &&
                Vector2Distance(world, gs.lairs[i].pos) < TOWN_CLICK_RADIUS)
                in.clickLair = i;
    }
    in.openLedger = IsKeyPressed(KEY_B);         // the kingdom ledger (O1)
    if (IsKeyPressed(KEY_F5)) in.saveSlot = 1;   // quicksave slots (N3)
    if (IsKeyPressed(KEY_F6)) in.saveSlot = 2;
    if (IsKeyPressed(KEY_F7)) in.saveSlot = 3;
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
    if (gs.siegePrompt >= 0) {   // the assault choice eats 1-3 (N1)
        if (IsKeyPressed(KEY_ONE))    in.menuChoice = 1;
        if (IsKeyPressed(KEY_TWO))    in.menuChoice = 2;
        if (IsKeyPressed(KEY_THREE))  in.menuChoice = 3;
        if (IsKeyPressed(KEY_ESCAPE)) in.menuChoice = 4;
    } else {
        if (IsKeyPressed(KEY_ONE)) in.joinSide = 1;
        if (IsKeyPressed(KEY_TWO)) in.joinSide = 2;
    }
    in.restart   = IsKeyPressed(KEY_R);
    in.crown     = IsKeyPressed(KEY_K);
    in.rallyLords = IsKeyPressed(KEY_J);
    in.openParty = IsKeyPressed(KEY_P);
    in.openInventory = IsKeyPressed(KEY_I);
    in.openCharacter = IsKeyPressed(KEY_C);
    in.quickSave = IsKeyPressed(KEY_F5);
    in.quickLoad = IsKeyPressed(KEY_F9);
    in.openSettings = IsKeyPressed(KEY_O);
    return in;
}

void CampaignUpdate(GameState& gs, float dt, const CampaignInput& in) {
    const Content& c = gs.content;

    if (gs.screen == Screen::BattleResult) {
        ApplyBattleResult(gs);
        // The aftermath may chain into another fight (tournament rounds, K3);
        // only settle back onto the map when it didn't redirect.
        if (gs.screen == Screen::BattleResult) gs.screen = Screen::Campaign;
    }
    gs.battleReportTimer = fmaxf(0.0f, gs.battleReportTimer - dt);

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

    // ---- call your lords to the banner (K5, crowned only) ----
    if (in.rallyLords && gs.crowned) {
        gs.lordsRally     = true;
        gs.lordsRallyPos  = gs.player.pos;
        gs.lordsRallyDays = 3.0f;   // TODO(balance)
        gs.resultText = "Your lords ride to your banner.";
    }

    // ---- claim your own crown (F3) ----
    // Two settlements make a realm. Crowning while sworn is rebellion: the
    // oath breaks and the liege remembers. Every other crown answers a new
    // claimant with war. TODO(balance): the settlement requirement, deltas.
    if (in.crown && !gs.crowned) {
        int owned = 0;
        for (const Town& t : gs.towns)
            if (t.owner == c.playerFaction) owned++;
        if (owned >= 2) {
            gs.crowned = true;
            const int n = c.factions.size();
            if (gs.liege >= 0) {
                NudgeRelation(gs, gs.liege, -40);
                if ((int)gs.hostile.size() == n * n)
                    gs.hostile[(size_t)c.playerFaction * n + gs.liege] =
                        gs.hostile[(size_t)gs.liege * n + c.playerFaction] = 1;
                gs.liege = -1;
            }
            for (int f = 0; f < n; ++f) {
                if (f == c.playerFaction || !c.factions[f].kingdom) continue;
                NudgeRelation(gs, f, -20);
                if ((int)gs.hostile.size() == n * n)
                    gs.hostile[(size_t)c.playerFaction * n + f] =
                        gs.hostile[(size_t)f * n + c.playerFaction] = 1;
            }
            gs.resultText = "YOU CLAIM A CROWN.  Every throne in the land answers with war.";
            SfxPlay(Sfx::Fanfare);
        } else {
            gs.resultText = "A crown needs a realm: hold two settlements first.";
        }
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
    if (in.openSettings) {
        gs.settingsBack = Screen::Campaign;
        gs.screen = Screen::Settings;
        return;
    }

    // The assault choice (N1): storm now, or camp and build engines while
    // the garrison musters on. TODO(balance): build times.
    if (gs.siegePrompt >= 0 && in.menuChoice != 0) {
        const int target = gs.siegePrompt;
        const bool village = gs.towns[target].type == SettlementType::Village;
        gs.siegePrompt = -1;
        switch (in.menuChoice) {
            case 1:   // take it — walls storm at the gate, fields just fight
                gs.siegeLaunchPrep  = 0;
                gs.raidingVillage   = false;
                gs.siegeTownIndex   = target;
                gs.battlePartyIndex = -1;
                gs.battleAllyIndex  = -1;
                gs.screen = Screen::Battle;
                return;
            case 2:
                if (village) {   // put it to the torch (P1): same fight,
                    gs.raidingVillage   = true;   // different aftermath
                    gs.siegeLaunchPrep  = 0;
                    gs.siegeTownIndex   = target;
                    gs.battlePartyIndex = -1;
                    gs.battleAllyIndex  = -1;
                    gs.screen = Screen::Battle;
                    return;
                }
                [[fallthrough]];
            case 3: { // engineering (walled targets only, N1)
                if (village) break;
                gs.siegeCampTown = target;
                gs.siegeCampPrep = in.menuChoice - 1;
                gs.siegeCampDays = (float)(in.menuChoice - 1);
                // The warband pitches camp under the walls.
                const Vector2 tp = gs.towns[target].pos;
                gs.player.pos = { tp.x + SIEGE_REACH, tp.y + SIEGE_REACH };
                gs.resultText = in.menuChoice == 2
                    ? "The camp rings with saws. Ladders by tomorrow."
                    : "A tower rises in the camp. Two days.";
                break;
            }
            default: break;   // thought better of it
        }
    }

    // The siege camp (N1): stay close while the engines are built; wander
    // off and the work is abandoned. Time must pass (wait/travel) to build.
    if (gs.siegeCampTown >= 0) {
        Town& target = gs.towns[gs.siegeCampTown];
        if (!AtWar(gs, target.owner, c.playerFaction)) {
            gs.siegeCampTown = -1;   // peace overtook the war
        } else if (Vector2Distance(gs.player.pos, target.pos) >
                   SIEGE_REACH * 4.0f) {
            gs.siegeCampTown = -1;
            gs.resultText = "The half-built engines are left to rot.";
        } else if (gs.siegeCampDays <= 0) {
            gs.siegeLaunchPrep  = gs.siegeCampPrep;   // the engines roll out
            gs.siegeTownIndex   = gs.siegeCampTown;
            gs.siegeCampTown    = -1;
            gs.battlePartyIndex = -1;
            gs.battleAllyIndex  = -1;
            gs.screen = Screen::Battle;
            return;
        }
    }

    if (in.openLedger) { gs.screen = Screen::Kingdom; return; }   // O1

    // Quicksave (N3): three slots on F5-F7, right from the saddle.
    if (in.saveSlot >= 1 && in.saveSlot <= 3) {
        const bool ok = SaveGame(gs, SaveSlotPath(in.saveSlot));
        gs.resultText = ok ? TextFormat("Saved to slot %d.", in.saveSlot)
                           : "The save failed.";
    }

    // ---- enter (friendly) or assault (hostile) a settlement ----
    if (in.clickSettlement >= 0 && in.clickSettlement < (int)gs.towns.size()) {
        Town& t = gs.towns[in.clickSettlement];
        if (AtWar(gs, t.owner, c.playerFaction)) {
            if (t.garrisonSize() <= 0) {
                // Nobody mans the walls — it simply changes hands.
                t.owner = c.playerFaction;
                gs.resultText = TextFormat("%s is undefended. It is yours.", t.name.c_str());
            } else if (gs.player.totalTroops() > 0) {
                // Walls or fields, the same question opens (N1/P1):
                // conquest, engineering, or — at a village — plunder.
                gs.siegePrompt = in.clickSettlement;
            }
        } else {
            // Grain deliveries (F4) pay at the destination gate.
            if (gs.activeQuest >= 0 && in.clickSettlement == gs.questTown &&
                c.quests[gs.activeQuest].type == QuestType::DeliverGrain) {
                const int g = c.goods.find("grain");
                const int need = c.quests[gs.activeQuest].amount;
                if (g >= 0 && g < (int)gs.goods.size() && gs.goods[g] >= need) {
                    gs.goods[g] -= need;
                    CompleteQuest(gs);
                }
            }
            // Walking into a feast (M5): the hall notices a famous guest.
            if (in.clickSettlement == gs.feastTown && gs.feastDays > 0 &&
                !gs.feastAttended) {
                gs.feastAttended = true;
                NudgeRelation(gs, gs.feastFaction, +5);   // TODO(balance)
                gs.renown += 1;
                gs.resultText = TextFormat(
                    "You join the feast at %s. The hall drinks to your name.",
                    gs.towns[in.clickSettlement].name.c_str());
            }
            gs.currentSettlement = in.clickSettlement;
            gs.screen = Screen::Settlement;
            return;
        }
    }

    // ---- storm a bandit den (H2) ----
    // The den's defenders muster as a fresh party; burning it out (victory
    // over that party) destroys the den for good.
    if (in.clickLair >= 0 && in.clickLair < (int)gs.lairs.size() &&
        gs.lairs[in.clickLair].alive && gs.player.totalTroops() > 0) {
        const Lair& l = gs.lairs[in.clickLair];
        Party den = MakeParty(c, l.faction, l.pos);
        for (int& n : den.troopCounts) n *= 2;   // dens defend hard; TODO(balance)
        den.engaged = true;
        gs.parties.push_back(den);
        gs.lairBattle       = in.clickLair;
        gs.battlePartyIndex = (int)gs.parties.size() - 1;
        gs.battleAllyIndex  = -1;
        gs.screen = Screen::Battle;
        return;
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
        gs.player.pos = Vector2Add(gs.player.pos,
            Vector2Scale(move, PARTY_SPEED * TravelSpeedFactor(gs, gs.player.pos) * dt));
    }
    gs.player.pos.x = Clamp(gs.player.pos.x, 0, MAP_SIZE);
    gs.player.pos.y = Clamp(gs.player.pos.y, 0, MAP_SIZE);

    // Answering the summons (K5): reach the invested town while it stands.
    if (gs.musterTown >= 0) {
        if (gs.liege < 0) {
            gs.musterTown = -1;   // the oath is gone, and the duty with it
        } else if (Vector2Distance(gs.player.pos, gs.towns[gs.musterTown].pos) <
                   SIEGE_REACH * 1.5f) {
            NudgeRelation(gs, gs.liege, +10);
            gs.resultText = "You answered your liege's summons. It is remembered.";
            gs.musterTown = -1;
        }
    }

    // ---- world simulation (advances only while time flows) ----
    if (sim > 0.0f) {
        // Roaming + pursuit: each free party steers by its behaviour toward the
        // nearest hostile party it can see (the player, or another faction).
        for (int i = 0; i < (int)gs.parties.size(); ++i) {
            Party& e = gs.parties[i];
            if (!e.alive || e.engaged) continue;

            Vector2 target;
            bool haveTarget = UpdateRest(gs, e, sim, target);

            // Caravans (E3) ply between their faction's settlements; arriving
            // fattens the destination's prosperity, then they walk the next leg.
            if (!haveTarget && e.caravan) {
                const bool destBad =
                    e.caravanTo < 0 || e.caravanTo >= (int)gs.towns.size() ||
                    (TradesAnywhere(gs, e.faction)
                         ? AtWar(gs, e.faction, gs.towns[e.caravanTo].owner)
                         : gs.towns[e.caravanTo].owner != e.faction);
                if (destBad)
                    e.caravanTo = NearestTradeStop(gs, e.faction, e.pos, e.caravanTo);
                if (e.caravanTo >= 0) {
                    Town& dest = gs.towns[e.caravanTo];
                    if (Vector2Distance(e.pos, dest.pos) < 30.0f) {
                        dest.prosperity = std::min(dest.prosperity + 5, 150);  // TODO(balance)
                        // Unload the freight into the destination's market —
                        // caravans genuinely move wares between towns. A
                        // player convoy sells its load (M4): each unit fetches
                        // the live sell price as the shelf fills, and the
                        // proceeds ride home to the ledger.
                        int revenue = 0;
                        if ((int)dest.stock.size() >= c.goods.size())
                            for (int g = 0; g < (int)e.cargo.size() &&
                                            g < c.goods.size(); ++g)
                                while (e.cargo[g] > 0) {
                                    if (e.faction == c.playerFaction)
                                        revenue += MarketSellPrice(c, dest, g);
                                    dest.stock[g]++;
                                    e.cargo[g]--;
                                }
                        if (e.faction == c.playerFaction && revenue > 0) {
                            gs.gold += revenue;
                            gs.resultText = TextFormat(
                                "Your caravan sold its load in %s: +%d gold "
                                "(cost %d).", dest.name.c_str(), revenue,
                                e.cargoCost);
                            e.cargoCost = 0;
                        }
                        e.caravanTo = NearestTradeStop(gs, e.faction, e.pos, e.caravanTo);
                        LoadCaravanCargo(c, dest, e);   // stock up for the next leg
                        if (e.faction == c.playerFaction)
                            gs.gold -= e.cargoCost;     // the buyer pays (M4)
                    }
                    if (e.caravanTo >= 0) {
                        target     = gs.towns[e.caravanTo].pos;
                        haveTarget = true;
                        e.state    = PartyState::Travelling;
                    }
                }
            }

            // A crowned ruler's rally overrides a lord's own plans (K5).
            if (!haveTarget && !e.lord.empty() && e.faction == c.playerFaction &&
                gs.lordsRally) {
                target     = gs.lordsRallyPos;
                haveTarget = true;
                e.state    = PartyState::Travelling;
            }

            // Lords wage the settlement war: march on the nearest hostile
            // settlement they outmatch and invest it on arrival.
            if (!haveTarget && !e.lord.empty()) {
                int  bestTown = -1;
                float bestD   = 1e9f;
                for (int ti = 0; ti < (int)gs.towns.size(); ++ti) {
                    const Town& t = gs.towns[ti];
                    if (!AtWar(gs, e.faction, t.owner)) continue;
                    if (t.garrisonSize() * 2 > e.totalTroops()) continue;  // TODO(balance)
                    const float d = Vector2Distance(e.pos, t.pos);
                    if (d < bestD) { bestD = d; bestTown = ti; }
                }
                if (bestTown >= 0) {
                    if (bestD < SIEGE_REACH) {
                        e.engaged = true;
                        e.state   = PartyState::Besieging;
                        gs.aiSieges.push_back({ i, bestTown, AI_SIEGE_TIME });
                        // Sound the alarm when it's YOUR settlement invested.
                        if (gs.towns[bestTown].owner == c.playerFaction)
                            gs.resultText = TextFormat("%s IS UNDER SIEGE by Lord %s!",
                                                       gs.towns[bestTown].name.c_str(),
                                                       e.lord.c_str());
                        // A liege's siege summons the sworn (K5).
                        else if (e.faction == gs.liege && gs.musterTown < 0) {
                            gs.musterTown = bestTown;
                            gs.musterDays = 3.0f;   // TODO(balance)
                            gs.resultText = TextFormat(
                                "YOUR LIEGE SUMMONS YOU to the siege of %s! Ride to it.",
                                gs.towns[bestTown].name.c_str());
                        }
                        continue;
                    }
                    target     = gs.towns[bestTown].pos;
                    haveTarget = true;
                    e.state    = PartyState::Travelling;
                }
            }

            if (!haveTarget) {
                const PartyBehavior behavior = c.factions[e.faction].behavior;
                const Foe foe = NearestHostile(gs, c, i);
                target = SteerTarget(e, behavior, foe, sim);
            }
            const Vector2 dir = Vector2Subtract(target, e.pos);
            if (Vector2Length(dir) > 5)
                e.pos = Vector2Add(e.pos, Vector2Scale(Vector2Normalize(dir),
                    PARTY_SPEED * 0.7f * TravelSpeedFactor(gs, e.pos) * sim));
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
                if (!AtWar(gs, a.faction, b.faction)) continue;
                if (Vector2Distance(a.pos, b.pos) < PARTY_COLLIDE_DIST) {
                    a.engaged = b.engaged = true;
                    a.state = b.state = PartyState::Engaged;
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
            if (AtWar(gs, e.faction, c.playerFaction) &&
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
            if (lr.timer <= 0) {
                Vector2 home = FactionHome(gs, lr.faction);
                for (const Town& t : gs.towns)   // a landed lord rises at his fief (M3)
                    if (t.fiefLord == lr.name && t.owner == lr.faction) {
                        home = t.pos;
                        break;
                    }
                gs.parties.push_back(MakeLordParty(c, lr.faction, lr.name, home));
            }
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
            // Hostile hands seize enterprises before the books are drawn.
            for (int ti = 0; ti < (int)gs.towns.size() && ti < (int)gs.enterpriseAt.size(); ++ti) {
                const int e = gs.enterpriseAt[ti];
                if (!c.enterprises.valid(e)) continue;
                if (AtWar(gs, gs.towns[ti].owner, c.playerFaction)) {
                    gs.enterpriseAt[ti] = -1;
                    gs.resultText = TextFormat("Your %s in %s is SEIZED by the enemy!",
                                               c.enterprises[e].name.c_str(),
                                               gs.towns[ti].name.c_str());
                }
            }

            // One purse, all flows (S5): income and enterprises in; troop
            // wages, landless lords' retainers and garrison pay out.
            const DayLedger L = ComputeLedger(gs);
            const int income = L.income + L.enterprise;
            const int wages  = L.wages + L.lordPay + L.garrisonPay;
            gs.gold += L.net();

            // Supply (P5): an army marches on its stomach. The warband eats
            // grain from the saddlebags — one unit per ten mouths — then
            // forages on coin; when both fail, hungry men drift away in the
            // night. TODO(balance): rates and the forage price.
            const int mouths = 1 + gs.player.totalTroops() / 10;
            int eaten = 0;
            const int grainG = c.goods.find("grain");
            if (grainG >= 0 && grainG < (int)gs.goods.size())
                while (eaten < mouths && gs.goods[grainG] > 0) {
                    gs.goods[grainG]--;
                    eaten++;
                }
            const int forage = (mouths - eaten) * 3;
            std::string supplyNote;
            if (eaten >= mouths) {
                supplyNote = TextFormat("  Ate %d grain.", eaten);
            } else if (gs.gold >= forage) {
                gs.gold -= forage;
                supplyNote = TextFormat("  Foraged (-%d gold).", forage);
            } else {
                const int lost = 1 + gs.player.totalTroops() / 10;
                RemoveTroops(c, gs.player, lost);
                supplyNote = TextFormat("  YOUR MEN STARVE - %d desert!", lost);
            }

            gs.resultText = TextFormat("Day %d:  +%d from your lands, -%d in wages.%s",
                                       gs.day, income, wages, supplyNote.c_str());

            // The wounded heal (S3): one man per line rejoins each dawn,
            // party cap willing. TODO(balance): the rate.
            {
                int back = 0;
                for (int t = 0; t < (int)gs.wounded.size() &&
                                t < (int)gs.player.troopCounts.size(); ++t)
                    if (gs.wounded[t] > 0 &&
                        gs.player.totalTroops() < PartyCap(gs)) {
                        gs.wounded[t]--;
                        gs.player.troopCounts[t]++;
                        back++;
                    }
                if (back > 0)
                    gs.resultText += TextFormat("  %d wounded return.", back);
            }

            // Autosave each dawn (Q5): a crash costs a day, not a career.
            SaveGame(gs, AutoSavePath());

            // A black name costs company (P3): each dawn, one honorable
            // companion walks out while your honor sits at -3 or worse.
            // TODO(balance): the threshold.
            if (gs.honor <= -3)
                for (int tr = 0; tr < c.troops.size(); ++tr) {
                    const TroopDef& td = c.troops[tr];
                    if (!td.companion || td.temper != "honorable" ||
                        gs.player.troopCounts[tr] <= 0) continue;
                    gs.player.troopCounts[tr] = 0;
                    gs.resultText = TextFormat(
                        "%s leaves your company: \"Find a better road, or "
                        "find another sword.\"", td.name.c_str());
                    break;   // one departure a day — a slow bleed, not a rout
                }

            // Markets live: a settlement produces what it makes (+1/day of
            // its cheap source goods, to a cap) and eats what it wants
            // (-1/day of its dear imports) — scarcity that caravans and the
            // player's runs genuinely relieve. Castles just keep quarters
            // stocked. TODO(balance): all rates and caps.
            for (Town& t : gs.towns) {
                const bool castle = t.type == SettlementType::Castle;
                for (int g = 0; g < (int)t.stock.size(); ++g) {
                    if (castle) {
                        if (t.stock[g] < 5) t.stock[g]++;
                        continue;
                    }
                    const bool makes = g < (int)t.priceOffset.size() &&
                                       t.priceOffset[g] < 100;
                    if (makes) { if (t.stock[g] < 20) t.stock[g]++; }
                    else if (t.stock[g] > 0) t.stock[g]--;
                }
            }
            // Feasts (M5): every few days a kingdom at peace with the player
            // throws one at its first town. TODO(balance): cadence/length.
            gs.feastDays -= 1.0f;
            if (gs.feastDays <= 0) {
                if (gs.feastTown >= 0) { gs.feastTown = -1; gs.feastFaction = -1; }
                if (gs.day % 4 == 2) {
                    for (int k = 0; k < c.factions.size(); ++k) {
                        const int f = (gs.day / 4 + k) % c.factions.size();
                        if (!c.factions[f].kingdom || f == c.playerFaction ||
                            AtWar(gs, f, c.playerFaction)) continue;
                        for (int t = 0; t < (int)gs.towns.size(); ++t)
                            if (gs.towns[t].owner == f &&
                                gs.towns[t].type == SettlementType::Town) {
                                gs.feastTown     = t;
                                gs.feastFaction  = f;
                                gs.feastDays     = 2.0f;
                                gs.feastAttended = false;
                                gs.resultText = TextFormat(
                                    "%s feasts at %s - all at peace are welcome.",
                                    c.factions[f].name.c_str(),
                                    gs.towns[t].name.c_str());
                                break;
                            }
                        if (gs.feastTown >= 0) break;
                    }
                }
            }

            DiplomacyDayTick(gs);   // truces run down; news overrides the ledger
            AlignWarsWithLiege(gs); // a vassal's wars follow the crown's (F2)

            // Ignored summons cost standing (K5); rally orders lapse.
            if (gs.musterTown >= 0 && (gs.musterDays -= 1.0f) <= 0) {
                NudgeRelation(gs, gs.liege, -15);
                gs.resultText = "You ignored your liege's summons. It is remembered.";
                gs.musterTown = -1;
            }
            if (gs.lordsRally && (gs.lordsRallyDays -= 1.0f) <= 0)
                gs.lordsRally = false;

            // Engines take shape in the siege camp (N1).
            if (gs.siegeCampTown >= 0) gs.siegeCampDays -= 1.0f;

            // AI armies eat too (Q2): a lord's host far from any friendly
            // settlement sheds men daily — long campaigns starve, and wars
            // end because armies wither, not only because they bleed.
            // TODO(balance): reach and rate.
            constexpr float SUPPLY_REACH = 600.0f;
            for (Party& p : gs.parties) {
                if (!p.alive || p.engaged || p.lord.empty()) continue;
                float nearest = 1e9f;
                for (const Town& t : gs.towns)
                    if (!AtWar(gs, p.faction, t.owner))
                        nearest = fminf(nearest,
                                        Vector2Distance(p.pos, t.pos));
                if (nearest > SUPPLY_REACH)
                    RemoveTroops(c, p, 1 + p.totalTroops() / 20);
            }

            // Renown fades (Q4): a week without new deeds costs a point —
            // the treadmill that keeps a long game moving. TODO(balance).
            if (gs.day > 0 && gs.day % 7 == 0 && gs.renown > 0) {
                gs.renown--;
                gs.resultText = TextFormat(
                    "Day %d:  the bards move on to newer songs (renown %d).",
                    gs.day, gs.renown);
            }

            // Warring lords bleed the countryside they march through (P1):
            // an enemy lord near a village drains its prosperity and stock.
            // TODO(balance): the reach and the bleed.
            for (const Party& p : gs.parties) {
                if (!p.alive || p.engaged || p.lord.empty()) continue;
                for (int ti = 0; ti < (int)gs.towns.size(); ++ti) {
                    Town& t = gs.towns[ti];
                    if (t.type != SettlementType::Village) continue;
                    if (!AtWar(gs, p.faction, t.owner)) continue;
                    if (Vector2Distance(p.pos, t.pos) > 200.0f) continue;
                    t.prosperity = std::max(30, t.prosperity - 10);
                    for (int g = 0; g < (int)t.stock.size(); ++g)
                        if (t.stock[g] > 0) { t.stock[g]--; break; }
                    if (t.owner == c.playerFaction)
                        gs.resultText = TextFormat(
                            "Lord %s is bleeding %s dry!", p.lord.c_str(),
                            t.name.c_str());
                }
            }

            // Lords change banners (R5): every fifth day, one lord of a
            // beaten crown (a settlement or less to its name) rides to the
            // ascendant one (three or more) — living politics between the
            // AI crowns; players win lords only by rebellion (O6).
            // TODO(balance): the thresholds and cadence.
            if (gs.day % 5 == 2) {
                std::vector<int> holdings(c.factions.size(), 0);
                for (const Town& t : gs.towns)
                    if (t.owner >= 0 && t.owner < c.factions.size())
                        holdings[t.owner]++;
                int best = -1;
                for (int f = 0; f < c.factions.size(); ++f)
                    if (f != c.playerFaction && c.factions[f].kingdom &&
                        (best < 0 || holdings[f] > holdings[best]))
                        best = f;
                if (best >= 0 && holdings[best] >= 3)
                    for (Party& p : gs.parties) {
                        if (!p.alive || p.engaged || p.lord.empty()) continue;
                        if (p.faction == best || p.faction == c.playerFaction)
                            continue;
                        if (!c.factions[p.faction].kingdom) continue;
                        if (holdings[p.faction] > 1) continue;
                        gs.resultText = TextFormat(
                            "Lord %s abandons %s for the %s banner!",
                            p.lord.c_str(), c.factions[p.faction].name.c_str(),
                            c.factions[best].name.c_str());
                        p.faction = best;
                        break;   // one turncoat a season
                    }
            }

            // World events (R4): every third day something happens somewhere
            // — announced as news, felt in the markets, or on the roads.
            // TODO(balance): the cadence.
            if (c.events.size() > 0 && !gs.towns.empty() && gs.day % 3 == 1) {
                const EventDef& ev = c.events[(gs.day / 3) % c.events.size()];
                Town& t = gs.towns[gs.day % (int)gs.towns.size()];
                t.prosperity = std::clamp(t.prosperity + ev.prosperityDelta, 30, 150);
                for (int g = 0; g < (int)t.stock.size(); ++g)
                    t.stock[g] = std::max(0, t.stock[g] + ev.stockDelta);
                const int raiders = c.factions.find("raiders");
                for (int n = 0; n < ev.spawnParties && raiders >= 0; ++n)
                    gs.parties.push_back(MakeParty(c, raiders,
                        { t.pos.x + Frand(-120, 120), t.pos.y + Frand(-120, 120) }));
                gs.resultText = TextFormat(ev.news.c_str(), t.name.c_str());
            }

            // Bandit dens breed a fresh band every other day (H2).
            for (Lair& l : gs.lairs) {
                if (!l.alive) continue;
                l.days += 1.0f;
                int alive = 0;
                for (const Party& p : gs.parties) if (p.alive) alive++;
                if (l.days >= 2.0f && alive < 14) {   // TODO(balance): rate/cap
                    l.days = 0;
                    gs.parties.push_back(MakeParty(c, l.faction, l.pos));
                }
            }

            // Caravans (E3): any faction holding two settlements keeps one
            // convoy on the road between them.
            for (int f = 0; f < c.factions.size(); ++f) {
                bool hasCaravan = false;
                for (const Party& p : gs.parties)
                    if (p.alive && p.caravan && p.faction == f) { hasCaravan = true; break; }
                if (hasCaravan) continue;
                int owned = 0, home = -1;
                for (int t = 0; t < (int)gs.towns.size(); ++t)
                    if (gs.towns[t].owner == f) { owned++; if (home < 0) home = t; }
                if (owned < 2) continue;
                const int to = NearestOwnedTown(gs, f, gs.towns[home].pos, home);
                if (to >= 0) {
                    gs.parties.push_back(MakeCaravan(c, f, gs.towns[home].pos, to));
                    LoadCaravanCargo(c, gs.towns[home], gs.parties.back());
                }
            }

            // The small folk take the roads (M6): keep a few traveller
            // bands walking town to town. They trade in a small way (the
            // caravan machinery), and bandits smell their packs.
            // TODO(balance): how many.
            {
                const int f_trav = c.factions.find("travellers");
                if (f_trav >= 0 && !gs.towns.empty()) {
                    int walking = 0;
                    for (const Party& p : gs.parties)
                        if (p.alive && p.faction == f_trav) walking++;
                    if (walking < 3) {
                        const int from = gs.day % (int)gs.towns.size();
                        const int to = NearestTradeStop(gs, f_trav,
                                                        gs.towns[from].pos, from);
                        if (to >= 0 &&
                            !AtWar(gs, f_trav, gs.towns[from].owner)) {
                            gs.parties.push_back(MakeCaravan(
                                c, f_trav, gs.towns[from].pos, to));
                            LoadCaravanCargo(c, gs.towns[from],
                                             gs.parties.back());
                        }
                    }
                }
            }

            // Every owner musters two soldiers a day toward the garrison cap
            // (roadmap B3c; U3 doubled the rate and the caps — walls are
            // worth manning now).
            for (Town& t : gs.towns) {
                if (t.owner < 0 || t.owner >= c.factions.size()) continue;
                const int cap = GarrisonCap(t.type);
                const std::vector<int>& roster = c.factions[t.owner].roster;
                for (int m = 0; m < 2 && !roster.empty() &&
                                t.garrisonSize() < cap; ++m) {
                    if ((int)t.garrison.size() < c.troops.size())
                        t.garrison.assign(c.troops.size(), 0);
                    t.garrison[roster[t.garrisonSize() % (int)roster.size()]]++;
                }
            }

            // Lords recruit the way the player does: a lord camped by any
            // friendly settlement takes on volunteers toward his full host,
            // instead of armies materialising. TODO(balance): the rate.
            constexpr int LORD_RECRUIT_RATE = 5;   // men per day at a settlement
            for (Party& p : gs.parties) {
                if (!p.alive || p.engaged || p.lord.empty()) continue;
                if (p.faction < 0 || p.faction >= c.factions.size()) continue;
                const std::vector<int>& roster = c.factions[p.faction].roster;
                const int want = c.factions[p.faction].lordPartySize;
                if (roster.empty()) continue;
                for (const Town& t : gs.towns) {
                    if (t.owner != p.faction ||
                        Vector2Distance(p.pos, t.pos) > REST_REACH * 2) continue;
                    if (p.totalTroops() < want) {
                        for (int i = 0; i < LORD_RECRUIT_RATE &&
                                        p.totalTroops() < want; ++i)
                            p.troopCounts[roster[p.totalTroops() %
                                                 (int)roster.size()]]++;
                    } else {
                        // A full host drills instead (L6): a couple of men a
                        // day step up their troop line, the way the player
                        // promotes veterans. TODO(balance): the rate.
                        constexpr int LORD_TRAIN_RATE = 2;
                        int drilled = 0;
                        for (int tt = 0; tt < (int)p.troopCounts.size() &&
                                         drilled < LORD_TRAIN_RATE; ++tt) {
                            const int up = c.troops[tt].upgradesTo;
                            while (up >= 0 && p.troopCounts[tt] > 0 &&
                                   drilled < LORD_TRAIN_RATE) {
                                p.troopCounts[tt]--;
                                p.troopCounts[up]++;
                                drilled++;
                            }
                        }
                    }
                    break;
                }
            }
            if (gs.gold < 0) {
                // The coffers ran dry: a share of the men drift away overnight.
                gs.gold = 0;
                const int leave = (gs.player.totalTroops() + 9) / 10;   // TODO(balance)
                RemoveTroops(c, gs.player, leave);
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

namespace {
// Painted ground: biome patches from the SAME noise that shapes battle
// terrain, so the map foreshadows the battlefield you'd fight on. Drawn in
// world coordinates; called once into the cached map texture (L5).
void PaintMapGround(const MapDef& m) {
    constexpr int CELL = 100;
    for (int gy = 0; gy < (int)MAP_SIZE / CELL; ++gy) {
        for (int gx = 0; gx < (int)MAP_SIZE / CELL; ++gx) {
            const Vector2 wp{ gx * CELL + CELL * 0.5f, gy * CELL + CELL * 0.5f };
            const float n1 = BiomeHillNoise(m, wp);
            const float n2 = BiomeForestNoise(m, wp);
            const Color ground = { (unsigned char)(66 + 20.0f * n1),
                                   (unsigned char)(98 + 14.0f * n2),
                                   (unsigned char)(48 + 10.0f * n1), 255 };
            DrawRectangle(gx * CELL, gy * CELL, CELL, CELL, ground);

            unsigned int h = (unsigned)(gx * 73856093) ^ (unsigned)(gy * 19349663);
            h ^= h >> 13;
            if (n2 > m.biome.forestThreshold) {   // forest clumps
                for (int t = 0; t < 5; ++t) {
                    h = h * 1664525u + 1013904223u;
                    const float tx = gx * CELL + (h & 63) + 16;
                    const float ty = gy * CELL + ((h >> 8) & 63) + 16;
                    DrawTriangle({ tx, ty }, { tx + 10, ty }, { tx + 5, ty - 16 },
                                 Color{ 34, 66, 34, 255 });
                }
            } else if (n1 > m.biome.mountainThreshold) {   // mountain ridges
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
}

// Roads thread the settlements together. Positions never move, so these are
// cached with the ground; only ownership (drawn per-frame) changes.
void PaintMapRoads(const GameState& gs) {
    for (int a = 0; a < (int)gs.towns.size(); ++a)
        for (int b = a + 1; b < (int)gs.towns.size(); ++b)
            if (Vector2Distance(gs.towns[a].pos, gs.towns[b].pos) <
                gs.content.map.roadLinkDist)
                DrawLineEx(gs.towns[a].pos, gs.towns[b].pos, 5,
                           Fade(Color{ 128, 106, 76, 255 }, 0.45f));
}

// The cached map ground (L5): biome paint + roads rendered once into a
// half-resolution texture instead of ~900 cells and thousands of triangles
// every frame. Rebuilt if the map size changes (a new map.cfg mid-run).
constexpr float MAP_TEX_SCALE = 0.5f;   // texels per world unit

const Texture2D& CachedMapTexture(const GameState& gs) {
    static RenderTexture2D tex{};
    static float builtFor = -1;
    if (builtFor != MAP_SIZE) {
        if (tex.id) UnloadRenderTexture(tex);
        tex = LoadRenderTexture((int)(MAP_SIZE * MAP_TEX_SCALE),
                                (int)(MAP_SIZE * MAP_TEX_SCALE));
        Camera2D shrink{};
        shrink.zoom = MAP_TEX_SCALE;
        BeginTextureMode(tex);
        ClearBackground(Color{ 40, 58, 36, 255 });
        BeginMode2D(shrink);
        PaintMapGround(gs.content.map);
        PaintMapRoads(gs);
        EndMode2D();
        EndTextureMode();
        builtFor = MAP_SIZE;
    }
    return tex.texture;
}
}   // namespace

void CampaignDraw(const GameState& gs) {
    const Content& c = gs.content;
    const Camera2D cam = CampaignCamera(gs);
    const int nearSkirmish = NearestSkirmishIndex(gs);

    SfxAmbience(0.07f);   // a soft wind over the overworld
    SfxMusic(0.06f);      // and a low drone beneath it (N5)

    const Texture2D& map = CachedMapTexture(gs);   // before BeginDrawing

    BeginDrawing();
    ClearBackground(Color{ 40, 58, 36, 255 });   // beyond the map's edge

    BeginMode2D(cam);
    // Render textures are Y-flipped; the negative source height rights it.
    DrawTexturePro(map,
                   Rectangle{ 0, 0, (float)map.width, -(float)map.height },
                   Rectangle{ 0, 0, MAP_SIZE, MAP_SIZE }, Vector2{ 0, 0 }, 0,
                   WHITE);

    DrawRectangleLinesEx(Rectangle{ 0, 0, MAP_SIZE, MAP_SIZE }, 6, DARKBROWN);

    for (const Town& t : gs.towns) {
        DrawEllipse((int)t.pos.x, (int)t.pos.y + 16, 30, 9, Fade(BLACK, 0.25f));
        switch (t.type) {
            // Silhouettes readable at a glance (T3): a village is a low
            // huddle of thatched huts, a town a walled sprawl of red roofs
            // around a gilt hall, a castle a tall grey keep between towers.
            case SettlementType::Village:
                for (const float hx : { -14.0f, 2.0f, -6.0f }) {
                    const float hy = hx == -6.0f ? 2.0f : -8.0f;
                    DrawRectangle((int)(t.pos.x + hx), (int)(t.pos.y + hy), 13, 10,
                                  Color{ 138, 104, 66, 255 });
                    DrawTriangle({ t.pos.x + hx - 2, t.pos.y + hy },
                                 { t.pos.x + hx + 15, t.pos.y + hy },
                                 { t.pos.x + hx + 6.5f, t.pos.y + hy - 9 },
                                 Color{ 196, 168, 92, 255 });   // thatch
                }
                break;
            case SettlementType::Town:
                DrawCircleLinesV(t.pos, 26, Fade(Color{ 120, 116, 110, 255 }, 0.9f));
                DrawCircleLinesV(t.pos, 27.5f, Fade(Color{ 120, 116, 110, 255 }, 0.5f));
                for (const float hx : { -18.0f, 4.0f, -8.0f }) {
                    const float hy = hx == -8.0f ? 4.0f : -12.0f;
                    DrawRectangle((int)(t.pos.x + hx), (int)(t.pos.y + hy), 14, 12,
                                  Color{ 168, 148, 120, 255 });
                    DrawTriangle({ t.pos.x + hx - 2, t.pos.y + hy },
                                 { t.pos.x + hx + 16, t.pos.y + hy },
                                 { t.pos.x + hx + 7, t.pos.y + hy - 10 },
                                 MAROON);   // red town roofs
                }
                DrawRectangle((int)t.pos.x - 4, (int)t.pos.y - 8, 9, 14,
                              Color{ 190, 170, 120, 255 });
                DrawTriangle({ t.pos.x - 6, t.pos.y - 8 }, { t.pos.x + 7, t.pos.y - 8 },
                             { t.pos.x + 0.5f, t.pos.y - 20 }, GOLD);   // the hall
                break;
            case SettlementType::Castle:
                for (const float txr : { -22.0f, 12.0f }) {   // corner towers
                    DrawRectangle((int)(t.pos.x + txr), (int)t.pos.y - 24, 10, 34,
                                  Color{ 128, 126, 132, 255 });
                    for (int b = 0; b < 10; b += 4)
                        DrawRectangle((int)(t.pos.x + txr) + b, (int)t.pos.y - 29,
                                      3, 5, Color{ 108, 106, 112, 255 });
                }
                DrawRectangle((int)t.pos.x - 12, (int)t.pos.y - 14, 24, 24,
                              Color{ 148, 146, 152, 255 });          // curtain
                DrawRectangle((int)t.pos.x - 7, (int)t.pos.y - 36, 14, 46,
                              Color{ 162, 160, 168, 255 });          // the keep
                for (int b = -7; b < 7; b += 5)
                    DrawRectangle((int)t.pos.x + b, (int)t.pos.y - 41, 3, 5,
                                  Color{ 128, 126, 132, 255 });
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
        DrawRectangle((int)t.pos.x - 44, (int)t.pos.y + 24, 120, 20, Fade(BLACK, 0.40f));
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
        // A small italic-feeling status word under the name, so the map reads as
        // a living world of parties each about their own business.
        ui::Text(PartyStateName(e.state), (int)e.pos.x - 20,
                 (int)e.pos.y - (isLord ? 30 : 22), 11, Fade(f.color, 0.75f));
    }

    // Besieged settlements: a pulsing ring in the attacker's colour.
    for (const AISiege& sg : gs.aiSieges) {
        if (sg.town < 0 || sg.town >= (int)gs.towns.size()) continue;
        if (sg.party < 0 || sg.party >= (int)gs.parties.size()) continue;
        const Town& t = gs.towns[sg.town];
        DrawCircleLines((int)t.pos.x, (int)t.pos.y, TOWN_CLICK_RADIUS + 10,
                        c.factions[gs.parties[sg.party].faction].color);
        DrawRectangle((int)t.pos.x - 48, (int)t.pos.y - 80, 104, 20, Fade(BLACK, 0.55f));
        ui::Text("UNDER SIEGE", (int)t.pos.x - 44, (int)t.pos.y - 78, 16, RED);
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

        // Travel pace, impossible to miss (T4): a ring under the banner in
        // the pace's colour, and the going named right at your feet —
        // green on roads, amber in the woods, slate in the mountains.
        const bool  onRoad = OnRoad(gs, p);
        const float pace   = TravelSpeedFactor(gs, p);
        const WorldTerrain wt = WorldTerrainAt(gs.content.map, p);
        Color pcol; const char* ptxt;
        if (onRoad)      { pcol = Color{ 90, 220, 90, 255 };  ptxt = "road  (full pace)"; }
        else if (pace >= 1.0f) { pcol = Fade(RAYWHITE, 0.6f); ptxt = nullptr; }
        else if (wt == WorldTerrain::Forest)
                         { pcol = Color{ 235, 170, 60, 255 }; ptxt = "forest  -30%"; }
        else             { pcol = Color{ 150, 165, 200, 255 };ptxt = "mountains  -45%"; }
        DrawRing(p, 14, 17, 0, 360, 32, Fade(pcol, 0.85f));
        if (ptxt) {
            DrawRectangle((int)p.x - 46, (int)p.y + 12, 130, 18, Fade(BLACK, 0.5f));
            ui::Text(ptxt, (int)p.x - 42, (int)p.y + 14, 14, pcol);
        }
    }
    // Bandit dens: a dark camp and a smoke smudge until someone burns them out.
    for (const Lair& l : gs.lairs) {
        if (!l.alive) continue;
        const Color fc = gs.content.factions.valid(l.faction)
                             ? gs.content.factions[l.faction].color : DARKGRAY;
        DrawCircleV(l.pos, 16, Fade(BLACK, 0.55f));
        DrawCircleV(l.pos, 10, Fade(fc, 0.8f));
        DrawCircleV({ l.pos.x + 6, l.pos.y - 14 }, 5, Fade(DARKGRAY, 0.5f));
        ui::Text("Den", (int)l.pos.x - 12, (int)l.pos.y + 20, 15, Fade(RAYWHITE, 0.75f));
    }

    // Lit windows: settlements glow after dark, before the night veil falls.
    const float dayFrac = gs.dayTimer / DAY_LENGTH;
    const bool  isNight = dayFrac >= 0.82f || dayFrac < 0.06f;
    if (isNight) {
        for (const Town& t : gs.towns) {
            DrawCircleV(t.pos, 26, Fade(Color{ 255, 190, 90, 255 }, 0.18f));
            DrawCircleV(t.pos, 12, Fade(Color{ 255, 210, 120, 255 }, 0.30f));
        }
    }
    EndMode2D();

    // ---- day/night veil: dawn gold, dusk amber, deep blue night ----
    {
        const int sw = GetScreenWidth(), sh = GetScreenHeight();
        if (dayFrac < 0.10f) {           // dawn burns off
            const float k = 1.0f - dayFrac / 0.10f;
            DrawRectangle(0, 0, sw, sh, Fade(Color{ 255, 170, 90, 255 }, 0.16f * k));
            DrawRectangle(0, 0, sw, sh, Fade(Color{ 20, 28, 60, 255 }, 0.30f * k));
        } else if (dayFrac >= 0.70f && dayFrac < 0.82f) {   // dusk
            const float k = (dayFrac - 0.70f) / 0.12f;
            DrawRectangle(0, 0, sw, sh, Fade(Color{ 235, 120, 50, 255 }, 0.30f * k));
        } else if (dayFrac >= 0.82f) {   // night deepens toward midnight
            const float k = fminf((dayFrac - 0.82f) / 0.06f, 1.0f);
            DrawRectangle(0, 0, sw, sh, Fade(Color{ 235, 120, 50, 255 }, 0.30f * (1.0f - k)));
            DrawRectangle(0, 0, sw, sh, Fade(Color{ 10, 14, 44, 255 }, 0.58f * k));
        }
    }

    // ---- battle aftermath card: a fading report panel over the map ----
    if (gs.battleReportTimer > 0 && !gs.battleReport.empty()) {
        const float a  = fminf(gs.battleReportTimer / 0.8f, 1.0f);   // fade out
        const int   sw = GetScreenWidth();
        const int   ph = 96 + 30 * (int)(gs.battleReport.size() - 1);
        const int   py = 120;
        const int   pw = 560;
        const int   px = (sw - pw) / 2;
        DrawRectangle(px, py, pw, ph, Fade(Color{ 16, 14, 12, 255 }, 0.85f * a));
        DrawRectangleLines(px, py, pw, ph, Fade(GOLD, 0.7f * a));
        const std::string& head = gs.battleReport.front();
        const bool grim = head.find("DEFEAT") != std::string::npos ||
                          head.find("REPELLED") != std::string::npos;
        ui::Title(head.c_str(), px + (pw - ui::MeasureTitle(head.c_str(), 40)) / 2,
                  py + 14, 40, Fade(grim ? RED : GOLD, a));
        int ly = py + 68;
        for (size_t i = 1; i < gs.battleReport.size(); ++i, ly += 30)
            ui::Text(gs.battleReport[i].c_str(), px + 28, ly, 20, Fade(RAYWHITE, 0.95f * a));
    }

    // ---- HUD ----
    const char* tod = dayFrac < 0.10f ? "dawn"
                    : dayFrac < 0.45f ? "morning"
                    : dayFrac < 0.70f ? "afternoon"
                    : dayFrac < 0.82f ? "evening" : "night";
    DrawRectangle(0, 0, GetScreenWidth(), 34, Fade(BLACK, 0.6f));
    {
        const WorldTerrain wt = WorldTerrainAt(gs.content.map, gs.player.pos);
        const bool road = OnRoad(gs, gs.player.pos);
        const char* going = road                          ? "on the road"
                            : wt == WorldTerrain::Forest   ? "in the woods (slow)"
                            : wt == WorldTerrain::Mountain ? "in the mountains (slow)"
                                                           : "on open ground";
        ui::Text(TextFormat("Day %d, %s   Gold: %d   Party: %d   %s", gs.day, tod,
                            gs.gold, gs.player.totalTroops(), going),
                 10, 8, 20, RAYWHITE);
    }

    // The assault choice (N1), centre stage until answered.
    if (gs.siegePrompt >= 0 && gs.siegePrompt < (int)gs.towns.size()) {
        const Town& t = gs.towns[gs.siegePrompt];
        const int px = GetScreenWidth() / 2 - 260, py = 200;
        DrawRectangle(px - 16, py - 16, 552, 208, Fade(BLACK, 0.8f));
        DrawRectangleLines(px - 16, py - 16, 552, 208, GOLD);
        const bool village = t.type == SettlementType::Village;
        ui::Title(TextFormat(village ? "THE FIELDS OF %s" : "THE WALLS OF %s",
                             t.name.c_str()), px, py, 30, GOLD);
        ui::Text(TextFormat("Garrison: %d — and it musters daily while you wait.",
                            t.garrisonSize()), px, py + 44, 18, Fade(RAYWHITE, 0.8f));
        if (village) {
            ui::Text("[1] Take it        (fight, and fly your banner)", px, py + 78, 20, RAYWHITE);
            ui::Text("[2] Put it to the torch  (loot and burn; a black deed)", px, py + 106, 20,
                     Fade(RED, 0.9f));
        } else {
            ui::Text("[1] Storm now      (the gate and two ladders)", px, py + 78, 20, RAYWHITE);
            ui::Text("[2] Build ladders  (1 day: two more climbing points)", px, py + 106, 20, RAYWHITE);
            ui::Text("[3] Build a tower  (2 days: a wide rolling ramp as well)", px, py + 134, 20, RAYWHITE);
        }
        ui::Text("[Esc] Think better of it", px, py + 166, 18, Fade(RAYWHITE, 0.6f));
    }
    if (gs.siegeCampTown >= 0)
        ui::Text(TextFormat("Siege camp at %s: %.0f day(s) until the engines are ready. Wait (SPACE).",
                            gs.towns[gs.siegeCampTown].name.c_str(),
                            fmaxf(gs.siegeCampDays, 0.0f)),
                 10, 38, 18, GOLD);
    else {
        // "What now?" (P4): one contextual pointer, always current.
        int banners = 0;
        for (const Town& t : gs.towns)
            if (t.owner != c.playerFaction) banners++;
        const char* what =
            gs.player.totalTroops() < 5
                ? "Your band is thin - recruit in a friendly settlement (click one)."
            : gs.crowned
                ? TextFormat("Take the land: %d banner(s) still stand against yours.", banners)
            : gs.liege >= 0
                ? "Serve your liege: answer summons, take work (G), earn your place."
            : gs.renown < RENOWN_TO_SWEAR
                ? "Win battles and tourneys - with renown, a crown may take your oath (V)."
                : "Trade, quest, and fight - or swear to a crown (V) and rise.";
        ui::Text(what, 10, 38, 17, Fade(RAYWHITE, 0.65f));
    }

    // Every door on one line (T7): the keys players kept not finding.
    {
        const char* keys = "[Wheel] zoom   [P]arty   [C]haracter   [I] bag   "
                           "[B] ledger   [O]ptions   [F5-F7] quicksave   "
                           "[Esc,Esc] save+quit   (load: title, L)";
        DrawRectangle(0, GetScreenHeight() - 36, GetScreenWidth(), 36,
                      Fade(BLACK, 0.72f));
        DrawRectangle(0, GetScreenHeight() - 37, GetScreenWidth(), 1,
                      Fade(GOLD, 0.35f));
        ui::Text(keys, 12, GetScreenHeight() - 29, 20, RAYWHITE);
    }

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

// Hired companions in troop-registry order — the Tab targets after the hero.
static std::vector<int> HiredCompanions(const GameState& gs) {
    std::vector<int> out;
    for (int t = 0; t < gs.content.troops.size(); ++t)
        if (gs.content.troops[t].companion && t < (int)gs.player.troopCounts.size() &&
            gs.player.troopCounts[t] > 0)
            out.push_back(t);
    return out;
}

void InventoryUpdate(GameState& gs, const CampaignInput& in) {
    const Content& c = gs.content;

    // Tab cycles who the bag fits: the hero, then each hired companion (K6).
    const std::vector<int> comps = HiredCompanions(gs);
    if (in.invCycleTarget) gs.invTarget = (gs.invTarget + 1) % (1 + (int)comps.size());
    if (gs.invTarget > (int)comps.size()) gs.invTarget = 0;

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
            Loadout& lo = gs.invTarget == 0
                              ? gs.playerHero.loadout
                              : CompanionGear(gs, comps[gs.invTarget - 1]);
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
    ui::Text("LMB pick up / place   E equip   Tab fit hero/companion   Esc / I back",
             ox, 116, 20, Fade(RAYWHITE, 0.7f));

    // Whose gear the bag fits right now (K6), and what they wear.
    const std::vector<int> comps = HiredCompanions(gs);
    const int  target = (gs.invTarget > (int)comps.size()) ? 0 : gs.invTarget;
    const bool onHero = target == 0;
    ui::Text(TextFormat("Fitting: %s",
                        onHero ? "You" : c.troops[comps[target - 1]].name.c_str()),
             ox + 560, 116, 20, GOLD);
    const Loadout& lo =
        onHero ? gs.playerHero.loadout
               : CompanionGear(const_cast<GameState&>(gs), comps[target - 1]);
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

    // Hover tooltip: what is this thing, and what does it do?
    const Vector2 m = GetMousePosition();
    const int hx = ((int)m.x - ox) / INV_CELL;
    const int hy = ((int)m.y - oy) / INV_CELL;
    if (m.x >= ox && m.y >= oy && hx >= 0 && hx < INV_W && hy >= 0 && hy < INV_H) {
        const int idx = ItemAtCell(gs, hx, hy);
        if (idx >= 0) {
            const InvItem& it = gs.inventory[idx];
            const char* line1;
            const char* line2;
            if (it.isWeapon) {
                const WeaponDef& w = c.weapons[it.handle];
                const char* cls = w.wclass == WeaponClass::TwoHanded ? "Two-handed"
                                : w.wclass == WeaponClass::Polearm   ? "Polearm"
                                : w.wclass == WeaponClass::Axe       ? "Axe"
                                : w.wclass == WeaponClass::Ranged    ? "Ranged"
                                                                     : "One-handed";
                line1 = TextFormat("%s  ·  %s", w.name.c_str(), cls);
                line2 = w.isRanged()
                    ? TextFormat("dmg %.0f   range %.0f", w.damage, w.missileRange)
                    : TextFormat("dmg %.0f   reach %.1f   swing %.1fs",
                                 w.damage, w.reach, w.swingTime);
            } else {
                const ArmorDef& a = c.armor[it.handle];
                const char* slot = a.slot == EquipSlot::Head ? "Head"
                                 : a.slot == EquipSlot::Body ? "Body"
                                 : a.slot == EquipSlot::Hands ? "Hands" : "Feet";
                line1 = TextFormat("%s  ·  %s", a.name.c_str(), slot);
                line2 = TextFormat("armour %d", a.armor);
            }
            const int tw = ui::Measure(line1, 20) > ui::Measure(line2, 18)
                               ? ui::Measure(line1, 20) : ui::Measure(line2, 18);
            const int bx = (int)m.x + 16, by = (int)m.y + 8;
            DrawRectangle(bx - 8, by - 6, tw + 16, 54, Fade(BLACK, 0.85f));
            DrawRectangleLines(bx - 8, by - 6, tw + 16, 54, Fade(GOLD, 0.6f));
            ui::Text(line1, bx, by, 20, RAYWHITE);
            ui::Text(line2, bx, by + 26, 18, Fade(RAYWHITE, 0.75f));
        }
    }

    EndDrawing();
}

// ---------------------------------------------------------------------------
// Marketplace (direction E1): buy/sell stackable trade goods at a settlement.
// Prices come from GoodDef::basePrice scaled by the town's per-good offset;
// selling pays a flat fraction of the buy price so round-trips cost gold.
// ---------------------------------------------------------------------------

// The forge's counter (O4): two equipment pieces rotate by town and day —
// what iron becomes once a town has worked it. TODO(balance): the price.
static constexpr int ARMS_PRICE = 50;

static void ArmsOnSale(const GameState& gs, InvItem& armorIt, InvItem& weaponIt) {
    const Content& c = gs.content;
    const int k = gs.currentSettlement * 3 + gs.day;
    armorIt.isWeapon  = false;
    armorIt.handle    = c.armor.size() > 0 ? k % c.armor.size() : -1;
    weaponIt.isWeapon = true;
    weaponIt.handle   = c.weapons.size() > 0 ? k % c.weapons.size() : -1;
}

int MarketBuyPrice(const Content& c, const Town& t, int g) {
    const int offset = g < (int)t.priceOffset.size() ? t.priceOffset[g] : 100;
    // Scarcity moves the needle: 4% per unit off a 10-unit par, clamped —
    // a glut is cheap, an empty shelf dear, so deliveries (caravan or
    // saddlebag) visibly shift the price. TODO(balance).
    const int stock    = g < (int)t.stock.size() ? t.stock[g] : 0;
    const int scarcity = Clamp(100 + (10 - stock) * 4, 70, 160);
    return std::max(1, c.goods[g].basePrice * offset * scarcity / 10000);
}

int MarketSellPrice(const Content& c, const Town& t, int g) {
    return MarketBuyPrice(c, t, g) * 3 / 4;   // TODO(balance): merchant's cut
}

void MarketUpdate(GameState& gs, const CampaignInput& in) {
    const Content& c = gs.content;
    if (gs.currentSettlement < 0 || gs.currentSettlement >= (int)gs.towns.size()) {
        gs.screen = Screen::Campaign;   // market with no settlement: bail out
        return;
    }
    Town& t = gs.towns[gs.currentSettlement];

    int carried = 0;
    for (int q : gs.goods) carried += q;

    if (in.buyGood >= 0 && in.buyGood < c.goods.size()) {
        const int g     = in.buyGood;
        const int price = MarketBuyPrice(c, t, g);
        if (t.stock[g] > 0 && gs.gold >= price && carried < GOODS_CAP) {
            gs.gold -= price;
            t.stock[g]--;
            gs.goods[g]++;
        }
    }
    // Arms from the forge (O4): keys past the wares buy today's pieces
    // into the tiled bag. Towns only — villages sell produce, not steel.
    if (t.type == SettlementType::Town &&
        (in.buyGood == c.goods.size() || in.buyGood == c.goods.size() + 1)) {
        InvItem armorIt, weaponIt;
        ArmsOnSale(gs, armorIt, weaponIt);
        InvItem pick = in.buyGood == c.goods.size() ? armorIt : weaponIt;
        if (pick.handle >= 0 && gs.gold >= ARMS_PRICE && AutoPlace(gs, pick)) {
            gs.gold -= ARMS_PRICE;
            gs.resultText = TextFormat("Bought: %s (%d gold).",
                pick.isWeapon ? c.weapons[pick.handle].name.c_str()
                              : c.armor[pick.handle].name.c_str(), ARMS_PRICE);
            SfxPlay(Sfx::Click);
        }
    }

    if (in.sellGood >= 0 && in.sellGood < c.goods.size()) {
        const int g = in.sellGood;
        if (gs.goods[g] > 0) {
            gs.gold += MarketSellPrice(c, t, g);
            gs.goods[g]--;
            t.stock[g]++;
        }
    }

    // Outfit a trade convoy (M4): it loads this market's surplus at the live
    // buy prices (paid from your purse, on top of the outfit fee), plies the
    // roads to markets at peace with you, and every sale rides home to the
    // ledger. Bandits can smell it like any other laden caravan.
    if (in.sendCaravan) {
        constexpr int CARAVAN_OUTFIT = 200;   // TODO(balance)
        if (gs.gold >= CARAVAN_OUTFIT) {
            const int to = NearestTradeStop(gs, c.playerFaction, t.pos,
                                            gs.currentSettlement);
            if (to >= 0) {
                gs.gold -= CARAVAN_OUTFIT;
                gs.parties.push_back(
                    MakeCaravan(c, c.playerFaction, t.pos, to));
                LoadCaravanCargo(c, t, gs.parties.back());
                gs.gold -= gs.parties.back().cargoCost;
                gs.resultText = TextFormat(
                    "Your caravan sets out for %s (outfit %d, cargo %d).",
                    gs.towns[to].name.c_str(), CARAVAN_OUTFIT,
                    gs.parties.back().cargoCost);
                SfxPlay(Sfx::Click);
            }
        } else {
            gs.resultText = "Outfitting a caravan costs 200 gold.";
        }
    }

    // Buy a business here (E4): towns only, one per town, deterministic pick
    // of the next unbuilt enterprise kind.
    if (in.buyEnterprise && t.type == SettlementType::Town &&
        gs.currentSettlement < (int)gs.enterpriseAt.size() &&
        gs.enterpriseAt[gs.currentSettlement] < 0 && c.enterprises.size() > 0) {
        const int kind = gs.currentSettlement % c.enterprises.size();
        const EnterpriseDef& e = c.enterprises[kind];
        if (gs.gold >= e.cost) {
            gs.gold -= e.cost;
            gs.enterpriseAt[gs.currentSettlement] = kind;
            gs.resultText = TextFormat("You now own the %s of %s.",
                                       e.name.c_str(), t.name.c_str());
        }
    }

    if (in.leaveSettlement) gs.screen = Screen::Settlement;   // back to the streets
}

void MarketDraw(const GameState& gs) {
    const Content& c = gs.content;
    BeginDrawing();
    ClearBackground(Color{ 24, 26, 30, 255 });

    const Town& t = gs.towns[gs.currentSettlement];
    const int   x = 120;
    ui::Title(TextFormat("%s MARKET", t.name.c_str()), x, 60, 44, GOLD);
    ui::Text("1-9 / click: buy one   Shift / right-click: sell one   "
             "[C] send a caravan (200)   Esc / M back",
             x, 116, 20, Fade(RAYWHITE, 0.7f));
    int carried = 0;
    for (int q : gs.goods) carried += q;
    ui::Text(TextFormat("Gold: %d      Saddlebags: %d / %d", gs.gold, carried,
                        GOODS_CAP), x, 150, 24, RAYWHITE);

    // Enterprise line (E4): what you own here, or the offer to buy.
    if (t.type == SettlementType::Town && gs.currentSettlement < (int)gs.enterpriseAt.size()) {
        const int owned = gs.enterpriseAt[gs.currentSettlement];
        if (c.enterprises.valid(owned))
            ui::Text(TextFormat("Your %s pays %d a day (by prosperity).",
                                c.enterprises[owned].name.c_str(),
                                c.enterprises[owned].dailyIncome),
                     x, 180, 20, Fade(GOLD, 0.9f));
        else if (c.enterprises.size() > 0) {
            const EnterpriseDef& e = c.enterprises[gs.currentSettlement % c.enterprises.size()];
            ui::Text(TextFormat("[B] Buy the %s  -  %d gold, pays %d a day",
                                e.name.c_str(), e.cost, e.dailyIncome),
                     x, 180, 20, Fade(RAYWHITE, 0.8f));
        }
    }

    int y = layout::MARKET_Y - 30;
    ui::Text("     ware          buy   sell   stock   yours", x, y, 18,
             Fade(RAYWHITE, 0.5f));
    y = layout::MARKET_Y;
    for (int g = 0; g < c.goods.size(); ++g) {
        const GoodDef& gd = c.goods[g];
        DrawHoverRow(layout::MARKET_X0, y, layout::MARKET_X1 - layout::MARKET_X0,
                     layout::MARKET_ROW_H);
        DrawRectangle(x, y + 3, 16, 16, gd.tint);
        ui::Text(TextFormat("[%d] %-12s %4d  %4d   %4d    %4d", g + 1,
                            gd.name.c_str(), MarketBuyPrice(c, t, g),
                            MarketSellPrice(c, t, g), t.stock[g], gs.goods[g]),
                 x + 26, y, 20, RAYWHITE);
        y += layout::MARKET_ROW_H;
    }

    // The forge's counter (O4): today's two pieces, towns only.
    if (t.type == SettlementType::Town) {
        InvItem armorIt, weaponIt;
        ArmsOnSale(gs, armorIt, weaponIt);
        y += 14;
        ui::Text("ARMS  (into your bag; rotates daily)", x, y, 18,
                 Fade(GOLD, 0.8f));
        y += 26;
        if (armorIt.handle >= 0)
            ui::Text(TextFormat("[7] %-16s %d gold",
                                c.armor[armorIt.handle].name.c_str(), ARMS_PRICE),
                     x, y, 20, RAYWHITE);
        y += 28;
        if (weaponIt.handle >= 0)
            ui::Text(TextFormat("[8] %-16s %d gold",
                                c.weapons[weaponIt.handle].name.c_str(), ARMS_PRICE),
                     x, y, 20, RAYWHITE);
    }

    EndDrawing();
}

// ---------------------------------------------------------------------------
// Settings screen (direction K1): a thin UI over src/settings — cycle the J4
// options live and write the cfg back on exit. Presentation only; nothing
// here touches simulation state.
// ---------------------------------------------------------------------------

void SettingsUpdate(GameState& gs, const CampaignInput& in) {
    Settings& s = GetSettings();
    switch (in.settingsRow) {
        case 0:
            s.fullscreen = !s.fullscreen;
            if (IsWindowReady()) ToggleFullscreen();
            break;
        case 1:   // draw distance steps through the sensible band
            s.lodDistance = s.lodDistance >= 90.0f ? 30.0f : s.lodDistance + 15.0f;
            break;
        case 2:
            s.particles = !s.particles;
            break;
        case 3:   // volume steps 0 / .25 / .5 / .75 / 1
            s.masterVolume += 0.25f;
            if (s.masterVolume > 1.01f) s.masterVolume = 0.0f;
            if (IsAudioDeviceReady()) SetMasterVolume(s.masterVolume);
            break;
        case 4:
            s.invertY = !s.invertY;
            break;
        default: break;
    }
    if (in.leaveSettlement) {
        SaveSettings();
        gs.screen = gs.settingsBack;
    }
}

void SettingsDraw(const GameState& gs) {
    (void)gs;
    const Settings& s = GetSettings();
    BeginDrawing();
    ClearBackground(Color{ 24, 26, 30, 255 });
    const int x = GetScreenWidth() / 2 - 300;
    ui::Title("SETTINGS", x, 60, 44, GOLD);
    ui::Text("[1-5 / click] change    [Esc / O] save and back", x, 120, 20,
             Fade(RAYWHITE, 0.7f));

    int y = layout::SETTINGS_Y;
    auto row = [&](int i, const char* label, const char* value) {
        DrawHoverRow(0, y, GetScreenWidth(), layout::SETTINGS_ROW_H);
        ui::Text(TextFormat("[%d]  %-18s %s", i + 1, label, value), x, y, 24, RAYWHITE);
        y += layout::SETTINGS_ROW_H;
    };
    row(0, "Fullscreen",    s.fullscreen ? "on" : "off");
    row(1, "Draw distance", TextFormat("%.0f", s.lodDistance));
    row(2, "Particles",     s.particles ? "on" : "off");
    row(3, "Volume",        TextFormat("%.0f%%", s.masterVolume * 100));
    row(4, "Invert look Y", s.invertY ? "on" : "off");

    ui::Text("Window size lives in assets/settings.cfg (takes effect on restart).",
             x, y + 20, 18, Fade(RAYWHITE, 0.55f));
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

    // Render the roster as a little 3D parade (into a texture, then blitted
    // as a panel). Windowed only — headless never calls Draw.
    static RenderTexture2D parade = { 0 };
    if (parade.id == 0) parade = LoadRenderTexture(420, 420);
    BeginTextureMode(parade);
    ClearBackground(Color{ 30, 33, 40, 255 });
    {
        Camera3D pc = { 0 };
        const float mid = (float)((int)rows.size() - 1) * 0.9f;
        pc.position = { mid, 2.2f, 6.0f };
        pc.target   = { mid, 1.0f, 0.0f };
        pc.up = { 0, 1, 0 };
        pc.fovy = 40;
        pc.projection = CAMERA_PERSPECTIVE;
        BeginMode3D(pc);
        BeginShaderMode(GetLitShader());
        DrawPlane({ mid, 0, 0 }, { 30, 12 }, Color{ 52, 58, 50, 255 });
        for (int i = 0; i < (int)rows.size() && i < 6; ++i) {
            Pose pose;
            pose.yaw = 0.35f;   // angled toward the viewer
            pose.accent = c.troops[rows[i]].accent;
            DrawCharacter(c, { i * 1.8f, 0, 0 }, c.troops[rows[i]].loadout, pose,
                          Color{ 40, 120, 255, 255 });
        }
        EndShaderMode();
        EndMode3D();
    }
    EndTextureMode();

    BeginDrawing();
    ClearBackground(Color{ 24, 26, 30, 255 });
    const int panelX = GetScreenWidth() / 2 - 360;

    // Daily ledger preview so roster decisions are financial decisions —
    // the same books the day tick settles (S5).
    const DayLedger L = ComputeLedger(gs);
    const int wages  = L.wages + L.lordPay + L.garrisonPay;
    const int income = L.income + L.enterprise;

    ui::Title("YOUR WARBAND", panelX, 60, 44, GOLD);
    ui::Text(TextFormat("Gold: %d    Troops: %d / %d    Daily: +%d income  -%d wages  (%+d)",
                        gs.gold, gs.player.totalTroops(), PartyCap(gs),
                        income, wages, income - wages),
             panelX, 120, 22, income >= wages ? RAYWHITE : Fade(RED, 0.9f));
    ui::Text("Survivors of won battles earn experience; spend it to promote them.",
             panelX, 150, 18, Fade(RAYWHITE, 0.7f));

    int y = layout::PARTY_Y;
    for (int slot = 0; slot < (int)rows.size(); ++slot) {
        const int t = rows[slot];
        const TroopDef& td = c.troops[t];
        const int xp = (t < (int)gs.troopXp.size()) ? gs.troopXp[t] : 0;
        DrawHoverRow(panelX, y, layout::PANEL_W, layout::PARTY_ROW_H);
        ui::Text(TextFormat("[%d]  %-10s x%-3d", slot + 1, td.name.c_str(),
                            gs.player.troopCounts[t]),
                 panelX, y, 24, RAYWHITE);
        // Wage column (R3): what this line costs the ledger each day.
        ui::Text(TextFormat("%2d g/day", gs.player.troopCounts[t] * td.wage),
                 panelX + 250, y + 4, 17, Fade(RAYWHITE, 0.55f));
        if (td.upgradesTo >= 0) {
            const bool can = xp >= td.upgradeXp;
            // Veterancy pip (R3): seasoning toward the next rank as a bar.
            const float fill = td.upgradeXp > 0
                ? fminf(1.0f, (float)xp / (float)td.upgradeXp) : 0.0f;
            DrawRectangle(panelX + 340, y + 8, 90, 8, Fade(BLACK, 0.5f));
            DrawRectangle(panelX + 340, y + 8, (int)(90 * fill), 8,
                          can ? LIME : Fade(GOLD, 0.8f));
            DrawRectangleLines(panelX + 340, y + 8, 90, 8, Fade(RAYWHITE, 0.3f));
            ui::Text(TextFormat("-> %s", c.troops[td.upgradesTo].name.c_str()),
                     panelX + 442, y + 3, 18, can ? LIME : Fade(RAYWHITE, 0.45f));
        } else {
            ui::Text("(elite)", panelX + 340, y + 3, 18, Fade(GOLD, 0.6f));
        }
        y += layout::PARTY_ROW_H;
    }
    if (rows.empty())
        ui::Text("Your warband is empty. Recruit in a friendly settlement.",
                 panelX, y, 22, Fade(RED, 0.8f));

    // The parade panel, framed on the right.
    const int paneX = GetScreenWidth() - 452;
    DrawTextureRec(parade.texture, { 0, 0, 420, -420 }, { (float)paneX, 180 }, WHITE);
    DrawRectangleLines(paneX, 180, 420, 420, Fade(GOLD, 0.5f));
    ui::Text("Your ranks on parade", paneX + 8, 186, 18, Fade(RAYWHITE, 0.7f));

    ui::Text("[1-9 / click] promote one    [Shift / right-click] dismiss one    [Esc / P] back",
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
    if (in.openSettings) {
        gs.settingsBack = Screen::Title;
        gs.screen = Screen::Settings;
        return true;
    }
    if (in.menuChoice != 0) SfxPlay(Sfx::Click);
    switch (in.menuChoice) {
        case 1:   // fresh world — choose who you were first (N2)
            gs.screen = Screen::Background;
            break;
        case 2:   // continue from the autosave, if there is one
            if (LoadGame(gs, AutoSavePath())) gs.resultText = "Welcome back.";
            gs.screen = Screen::Campaign;   // load failure = new game
            break;
        case 3:   // pick a save (N3)
            gs.screen = Screen::LoadMenu;
            break;
        case 4:
            return false;
        default: break;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Kingdom ledger (O1): everything a ruler needs on one page. Read-only —
// all of it is state the systems already keep; this is the view that makes
// the holdings feel like a kingdom.
// ---------------------------------------------------------------------------

void KingdomUpdate(GameState& gs, const CampaignInput& in) {
    if (in.leaveSettlement) { gs.screen = Screen::Campaign; return; }

    // Sue for peace (S1): pick a war row (1-9), pay tribute scaled by its
    // score, and the guns fall silent under an ordinary truce. Wars you
    // start, you can end. TODO(balance): tribute; AI always accepts (v1).
    if (in.menuChoice >= 1) {
        const Content& c = gs.content;
        const int n = c.factions.size();
        if ((int)gs.hostile.size() != (size_t)n * n) return;
        int row = 0;
        for (int f = 0; f < n; ++f) {
            if (f == c.playerFaction || !AtWar(gs, c.playerFaction, f)) continue;
            if (!c.factions[f].kingdom) continue;   // outlaws never treat
            if (++row != in.menuChoice) continue;
            const size_t ij = (size_t)c.playerFaction * n + f;
            const size_t ji = (size_t)f * n + c.playerFaction;
            const int tribute = 100 + gs.warScore[ij] * 5;   // TODO(balance)
            if (gs.gold < tribute) {
                gs.resultText = TextFormat(
                    "Peace with %s asks %d gold in tribute. You lack it.",
                    c.factions[f].name.c_str(), tribute);
                return;
            }
            gs.gold -= tribute;
            gs.hostile[ij] = gs.hostile[ji] = 0;
            gs.warScore[ij] = gs.warScore[ji] = 0;
            gs.truceDays[ij] = gs.truceDays[ji] = 4.0f;   // mirrors TRUCE_DAYS
            NudgeRelation(gs, f, +5);
            gs.resultText = TextFormat(
                "PEACE:  %d gold in tribute buys silence with %s.",
                tribute, c.factions[f].name.c_str());
            SfxPlay(Sfx::Fanfare);
            return;
        }
    }
}

void KingdomDraw(const GameState& gs) {
    const Content& c = gs.content;
    BeginDrawing();
    ClearBackground(Color{ 24, 26, 30, 255 });
    const int lx = 80, rx = GetScreenWidth() / 2 + 60;
    ui::Title("THE LEDGER", lx, 40, 44, GOLD);

    // ---- the realm's headline ----
    const char* rank = gs.crowned ? "Crowned ruler"
                       : gs.liege >= 0 ? TextFormat("Sworn to %s",
                                                    c.factions[gs.liege].name.c_str())
                                       : "Free captain";
    ui::Text(TextFormat("%s      Renown %d   Honor %+d      Gold %d", rank,
                        gs.renown, gs.honor, gs.gold), lx, 100, 22, RAYWHITE);
    if (gs.spouseFaction >= 0)
        ui::Text(TextFormat("Wed to Lady %s of %s", gs.spouseName.c_str(),
                            c.factions[gs.spouseFaction].name.c_str()),
                 lx, 128, 18, Fade(PINK, 0.85f));

    // ---- fiefs and their income ----
    int y = 170;
    ui::Text("FIEFS", lx, y, 22, GOLD); y += 30;
    int income = 0, granted = 0;
    for (const Town& t : gs.towns) {
        if (t.owner != c.playerFaction) continue;
        const int pay = SettlementIncome(t.type) * t.prosperity / 100;
        if (t.fiefLord.empty()) income += pay; else granted += pay;
        ui::Text(TextFormat("%-11s %-8s prosper %d%%   %s", t.name.c_str(),
                            SettlementTypeName(t.type), t.prosperity,
                            t.fiefLord.empty()
                                ? TextFormat("+%d/day", pay)
                                : TextFormat("held by Lord %s", t.fiefLord.c_str())),
                 lx, y, 19, t.fiefLord.empty() ? RAYWHITE : Fade(GOLD, 0.85f));
        y += 26;
        if (y > GetScreenHeight() - 160) break;
    }
    // One purse, all flows (S5) — the same books the day tick settles.
    const DayLedger L = ComputeLedger(gs);
    y += 8;
    ui::Text(TextFormat("Income %d + enterprises %d - wages %d - retainers %d"
                        " - garrisons %d  =  %+d a day    (lords keep %d)",
                        L.income, L.enterprise, L.wages, L.lordPay,
                        L.garrisonPay, L.net(), granted),
             lx, y, 20, L.net() >= 0 ? LIME : Fade(RED, 0.9f));

    // ---- lords of the realm ----
    int ry = 170;
    ui::Text("LORDS AFIELD", rx, ry, 22, GOLD); ry += 30;
    bool anyLord = false;
    for (const Party& p : gs.parties) {
        if (!p.alive || p.faction != c.playerFaction || p.lord.empty()) continue;
        anyLord = true;
        const int eff = EffectiveLordOpinion(const_cast<GameState&>(gs), p.lord);
        ui::Text(TextFormat("Lord %-9s %3d men   %-10s opinion %+d",
                            p.lord.c_str(), p.totalTroops(),
                            PartyStateName(p.state), eff),
                 rx, ry, 19, eff >= 0 ? RAYWHITE : Fade(RED, 0.9f));
        ry += 26;
    }
    if (!anyLord) {
        ui::Text("None ride under your banner.", rx, ry, 19, Fade(RAYWHITE, 0.5f));
        ry += 26;
    }
    for (const auto& pl : gs.capturedLords) {
        ui::Text(TextFormat("Lord %-9s of %s  — your prisoner", pl.first.c_str(),
                            c.factions.valid(pl.second)
                                ? c.factions[pl.second].name.c_str() : "?"),
                 rx, ry, 19, Fade(ORANGE, 0.9f));
        ry += 26;
    }

    // ---- wars ----
    ry += 12;
    ui::Text("WARS", rx, ry, 22, GOLD); ry += 30;
    bool atPeaceAll = true;
    int warRow = 0;
    const int nf = c.factions.size();
    for (int f = 0; f < nf; ++f) {
        if (f == c.playerFaction || !AtWar(gs, c.playerFaction, f)) continue;
        atPeaceAll = false;
        if (!c.factions[f].kingdom) {   // outlaws never treat
            ui::Text(TextFormat("At war with %-10s  (no quarter)",
                                c.factions[f].name.c_str()),
                     rx, ry, 19, Fade(RED, 0.7f));
            ry += 26;
            continue;
        }
        warRow++;
        const int tribute = (int)gs.hostile.size() == nf * nf
            ? 100 + gs.warScore[(size_t)c.playerFaction * nf + f] * 5 : 100;
        ui::Text(TextFormat("[%d] At war with %-10s  sue for peace: %d gold",
                            warRow, c.factions[f].name.c_str(), tribute),
                 rx, ry, 19, Fade(RED, 0.9f));
        ry += 26;
    }
    if (atPeaceAll)
        ui::Text("The realm is at peace.", rx, ry, 19, LIME);

    ui::Text("[Esc / B] close the book", lx, GetScreenHeight() - 44, 20,
             Fade(RAYWHITE, 0.7f));
    EndDrawing();
}

// ---------------------------------------------------------------------------
// Load menu (N3): the autosave and three quicksave slots, with a peeked
// day/gold line per file so the choice means something.
// ---------------------------------------------------------------------------

void LoadMenuUpdate(GameState& gs, const CampaignInput& in) {
    if (in.leaveSettlement) { gs.screen = Screen::Title; return; }
    if (in.menuChoice >= 1 && in.menuChoice <= 4) {
        const char* path = in.menuChoice == 1 ? AutoSavePath()
                                              : SaveSlotPath(in.menuChoice - 1);
        if (LoadGame(gs, path)) {
            gs.resultText = "Welcome back.";
            gs.screen = Screen::Campaign;
        }
        // A missing file just stays on the menu — the row said "empty".
    }
}

void LoadMenuDraw(const GameState& gs) {
    (void)gs;
    BeginDrawing();
    ClearBackground(Color{ 20, 22, 26, 255 });
    const int w = GetScreenWidth();
    const char* t = "LOAD GAME";
    ui::Title(t, (w - ui::MeasureTitle(t, 56)) / 2, 180, 56, GOLD);
    int y = layout::TITLE_Y;
    auto row = [&](int i, const char* label, const char* path) {
        DrawHoverRow(w / 2 - 260, y, 520, layout::TITLE_ROW_H);
        int day = 0, gold = 0;
        const bool have = PeekSave(path, day, gold);
        const char* txt = have
            ? TextFormat("[%d]  %-10s day %d, %d gold", i, label, day, gold)
            : TextFormat("[%d]  %-10s (empty)", i, label);
        ui::Text(txt, w / 2 - 250, y + 10, 26,
                 have ? RAYWHITE : Fade(RAYWHITE, 0.35f));
        y += layout::TITLE_ROW_H;
    };
    row(1, "Autosave", AutoSavePath());
    row(2, "Slot 1", SaveSlotPath(1));
    row(3, "Slot 2", SaveSlotPath(2));
    row(4, "Slot 3", SaveSlotPath(3));
    ui::Text("[Esc] back      (save on the map with F5 / F6 / F7)",
             w / 2 - 250, y + 16, 18, Fade(RAYWHITE, 0.6f));
    EndDrawing();
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
    const char* sub = "raise a warband - take the land - hold it - "
                      "until every banner on the map is yours";
    ui::Text(sub, (w - ui::Measure(sub, 22)) / 2, 260, 22, Fade(RAYWHITE, 0.7f));

    const bool haveSave = FileExists(AutoSavePath());
    int y = layout::TITLE_Y;
    auto option = [&](const char* txt, Color col) {
        DrawHoverRow(w / 2 - 210, y, 420, layout::TITLE_ROW_H);
        ui::Text(txt, (w - ui::Measure(txt, 30)) / 2, y, 30, col);
        y += layout::TITLE_ROW_H;
    };
    option("[N]  New Game", RAYWHITE);
    option("[C]  Continue", haveSave ? RAYWHITE : Fade(RAYWHITE, 0.35f));
    option("[L]  Load Game", RAYWHITE);
    option("[Esc]  Quit", Fade(RAYWHITE, 0.8f));

    EndDrawing();
}

// ---------------------------------------------------------------------------
// Character creation (N2): who were you before the warband? A background
// seeds gold, gear, fame and standing — flavour with mechanical teeth, all
// flat TODO(balance).
// ---------------------------------------------------------------------------

void ApplyBackground(GameState& gs, int choice) {
    const Content& c = gs.content;
    const int patrol = c.factions.find("patrol");
    switch (choice) {
        case 1:   // A noble's second son: name, plate, and doors that open.
            gs.renown += 5;
            gs.gold   += 200;
            NudgeRelation(gs, patrol, +10);
            if (c.armor.find("helmet") >= 0)
                gs.playerHero.loadout.set(EquipSlot::Head, c.armor.find("helmet"));
            gs.resultText = "A noble's second son rides out for a name of his own.";
            break;
        case 2: {  // A merchant's heir: coin, cargo, and a clean ledger.
            gs.gold += 400;
            gs.honor += 1;
            const int grain = c.goods.find("grain");
            if (grain >= 0) {
                if ((int)gs.goods.size() < c.goods.size())
                    gs.goods.assign(c.goods.size(), 0);
                gs.goods[grain] += 10;
            }
            gs.resultText = "A merchant's heir turns the family books into a warband.";
            break;
        }
        case 3: {  // A deserter: hard men, hard name, light purse.
            gs.gold = std::max(0, gs.gold - 150);
            gs.renown += 1;
            NudgeRelation(gs, patrol, -10);
            const int brigand = c.troops.find("brigand");
            if (brigand >= 0 && brigand < (int)gs.player.troopCounts.size())
                gs.player.troopCounts[brigand] += 3;
            gs.resultText = "A deserter gathers old comrades the crown would hang.";
            break;
        }
        default: break;
    }
}

void BackgroundUpdate(GameState& gs, const CampaignInput& in) {
    if (in.menuChoice >= 1 && in.menuChoice <= 3) {
        ApplyBackground(gs, in.menuChoice);
        SfxPlay(Sfx::Fanfare);
        gs.screen = Screen::Campaign;
    }
}

void BackgroundDraw(const GameState& gs) {
    (void)gs;
    BeginDrawing();
    ClearBackground(Color{ 20, 22, 26, 255 });
    const int w = GetScreenWidth();
    const char* t = "WHO WERE YOU?";
    ui::Title(t, (w - ui::MeasureTitle(t, 56)) / 2, 120, 56, GOLD);
    int y = layout::TITLE_Y - 100;
    auto option = [&](const char* head, const char* sub) {
        DrawHoverRow(w / 2 - 330, y, 660, layout::TITLE_ROW_H);
        ui::Text(head, w / 2 - 320, y, 26, RAYWHITE);
        ui::Text(sub, w / 2 - 320, y + 26, 17, Fade(RAYWHITE, 0.6f));
        y += layout::TITLE_ROW_H + 14;
    };
    option("[1]  A noble's second son",
           "+5 renown, +200 gold, a helmet, the patrols' favour");
    option("[2]  A merchant's heir",
           "+400 gold, 10 sacks of grain, +1 honor");
    option("[3]  A deserter",
           "3 brigands at your back, -150 gold, the patrols' suspicion");
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
    ui::Text(TextFormat("Renown: %d  (party cap %d)      Honor: %+d",
                        gs.renown, PartyCap(gs), gs.honor),
             panelX, 150, 20, Fade(GOLD, 0.9f));

    int y = layout::CHAR_Y;
    for (int a = 0; a < c.attributes.size(); ++a) {
        const AttributeDef& ad = c.attributes[a];
        const int v = a < (int)hero.attributes.size() ? hero.attributes[a] : 0;
        DrawHoverRow(panelX, y, layout::PANEL_W, layout::CHAR_ROW_H);
        ui::Text(TextFormat("[%d]  %-14s %d", a + 1, ad.name.c_str(), v),
                 panelX, y, 26, hero.attrPoints > 0 ? LIME : RAYWHITE);
        ui::Text(ad.hook.c_str(), panelX + 320, y + 4, 16, Fade(RAYWHITE, 0.55f));
        y += layout::CHAR_ROW_H;
    }

    ui::Text("Attribute effects arrive with the balancing pass - spend freely.",
             panelX, y + 10, 18, Fade(GOLD, 0.7f));

    // Standing with the powers of the land (F1).
    y += 48;
    ui::Text("STANDING", panelX, y, 22, GOLD);
    y += 30;
    for (int f = 0; f < c.factions.size(); ++f) {
        if (f == c.playerFaction) continue;
        const int r = f < (int)gs.relations.size() ? gs.relations[f] : 0;
        ui::Text(TextFormat("%-14s %+d", c.factions[f].name.c_str(), r), panelX, y,
                 20, r > 0 ? LIME : r < 0 ? Fade(RED, 0.9f) : Fade(RAYWHITE, 0.8f));
        y += 26;
    }
    // Named lords who remember you (N4); honor colours every reading.
    if (!gs.lordOpinion.empty()) {
        int ly = 180;
        const int lx = GetScreenWidth() - 340;
        ui::Text("LORDS", lx, ly, 22, GOLD);
        ly += 30;
        for (const auto& p : gs.lordOpinion) {
            const int eff = p.second + gs.honor;
            ui::Text(TextFormat("Lord %-10s %+d", p.first.c_str(), eff), lx, ly,
                     20, eff > 0 ? LIME : eff < 0 ? Fade(RED, 0.9f)
                                                  : Fade(RAYWHITE, 0.8f));
            ly += 26;
        }
    }

    ui::Text("[1-4 / click a row] spend a point    [Esc / C] back to the map",
             panelX, GetScreenHeight() - 48, 20, Fade(RAYWHITE, 0.7f));
    EndDrawing();
}

