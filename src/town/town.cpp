#include "town.h"
#include "../battle/character.h"   // the one humanoid renderer (battle owns it)
#include "../gfx.h"
#include "../sfx.h"
#include "../ui.h"
#include "raymath.h"
#include <cmath>
#include <vector>

namespace {

constexpr float TOWN_EDGE    = 55.0f;  // walkable half-size
constexpr float WALK_SPEED   = 6.0f;
constexpr float NPC_SPEED    = 2.2f;
constexpr float TAVERN_RANGE = 9.0f;   // close enough to recruit

// Deterministic per-town rng (same pattern as battle terrain).
struct Rng {
    unsigned int s;
    unsigned int next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float unit() { return (next() & 0xFFFFFF) / (float)0xFFFFFF; }
    float range(float a, float b) { return a + (b - a) * unit(); }
};

struct Building {
    Vector3 pos;      // centre on the ground
    Vector3 size;     // w, h, d
    Color   wall;
    Color   roof;
    bool    tavern = false;
    bool    flatTop = false;   // curtain walls / towers: no pitched roof
};

struct Npc {
    Vector3     pos;
    float       yaw = 0;
    float       walkPhase = 0;
    Vector2     target{};
    float       think = 0;
    Loadout     loadout;
    std::string line;   // what they say when you come close
};

struct TownScene {
    std::vector<Building> buildings;
    std::vector<Npc>      npcs;
    int     tavern = -1;
    bool    stalls = false;   // market stalls ring the plaza (N5; towns only)
    bool    menu   = true;    // the settlement menu (U4): GUI first, boots
                              // by choice ("Visit the settlement")

    Vector3 pPos{};
    float   yaw = 0, pitch = 0;
    float   walkPhase = 0;

    // Inside the tavern common room (separate little coordinate space).
    bool    inside = false;
    Vector3 iPos{};

    Vector2 lastMouse{};
    bool    hasLastMouse = false;
};

TownScene T;

// What the locals have to say. Some of it is stock small talk; some is real
// gossip composed from world state (sieges, who holds what) at entry time.
std::vector<std::string> GatherLines(const GameState& gs, bool guards) {
    std::vector<std::string> lines;
    if (guards) {
        lines.push_back("Keep your blade sheathed inside the walls.");
        lines.push_back("The lord watches from the keep.");
        lines.push_back("Quiet watch tonight. I don't trust it.");
    } else {
        lines.push_back("Fine weather for the harvest.");
        lines.push_back("Mind the well after dark, stranger.");
        lines.push_back("Soldiers drink the tavern dry these days.");
    }
    // Gossip: the war reaches every ear.
    const Content& c = gs.content;
    for (const AISiege& sg : gs.aiSieges)
        if (sg.town >= 0 && sg.town < (int)gs.towns.size())
            lines.push_back(TextFormat("They say %s is under siege!",
                                       gs.towns[sg.town].name.c_str()));
    for (const Town& t : gs.towns)
        if (t.owner >= 0 && t.owner < c.factions.size() &&
            AtWar(gs, t.owner, c.playerFaction))
            lines.push_back(TextFormat("%s flies the %s banner now. Dark days.",
                                       t.name.c_str(), c.factions[t.owner].name.c_str()));
    return lines;
}

// Take the oath at this settlement (F2). Returns what happened, or the
// refusal — shared by the V hotkey and the lord's court dialogue (K2).
std::string TrySwear(GameState& gs) {
    const Content& c = gs.content;
    if (gs.liege >= 0) return "You are already sworn.";
    const int f = AudienceFaction(gs);   // hall owner, or the hailed lord's crown (S4)
    const bool kingdom = f >= 0 && f != c.playerFaction &&
                         c.factions[f].kingdom && !c.factions[f].lords.empty();
    const int standing = (f >= 0 && f < (int)gs.relations.size()) ? gs.relations[f] : 0;
    if (!kingdom) return "No crown holds court here.";
    if (AtWar(gs, f, c.playerFaction)) return "You are at war with this crown.";
    if (standing < 0) return "Your name is mud with this crown. Mend it first.";
    if (gs.renown < RENOWN_TO_SWEAR)
        return TextFormat("A crown wants proven captains. Win renown first (%d/%d).",
                          gs.renown, RENOWN_TO_SWEAR);
    // A wronged first lord speaks against you at court (N4). TODO(balance).
    if (!c.factions[f].lords.empty() &&
        EffectiveLordOpinion(gs, c.factions[f].lords[0]) < -10)
        return TextFormat("Lord %s speaks against you. The crown listens to him.",
                          c.factions[f].lords[0].c_str());
    gs.liege = f;
    AlignWarsWithLiege(gs);
    int fief = -1;
    for (int t = 0; t < (int)gs.towns.size(); ++t)
        if (gs.towns[t].owner == f && gs.towns[t].type == SettlementType::Village) {
            fief = t;
            break;
        }
    std::string msg;
    if (fief >= 0) {
        gs.towns[fief].owner = c.playerFaction;
        msg = TextFormat("You are sworn to %s. %s is your fief.",
                         c.factions[f].name.c_str(), gs.towns[fief].name.c_str());
    } else {
        msg = TextFormat("You are sworn to %s. No fief yet - earn one.",
                         c.factions[f].name.c_str());
    }
    gs.resultText = msg;
    Chronicle(gs, TextFormat("Sworn to %s.", c.factions[f].name.c_str()));
    SfxPlay(Sfx::Fanfare);
    return msg;
}

// Take the local giver's quest (F4). Returns the offer, or why there is none —
// shared by the G hotkey and the lord's court dialogue (K2).
std::string TryQuest(GameState& gs) {
    const Content& c = gs.content;
    if (gs.activeQuest >= 0) return "Finish the task you carry first.";
    if (c.quests.size() == 0) return "No work today.";
    // A road parley (S4) has no hall: the lord speaks for the nearest one.
    int src = gs.currentSettlement;
    if (src < 0) {
        float bd = 1e9f;
        for (int t = 0; t < (int)gs.towns.size(); ++t) {
            const float d = Vector2Distance(gs.player.pos, gs.towns[t].pos);
            if (d < bd) { bd = d; src = t; }
        }
        if (src < 0) return "No work today.";
    }
    const int q = (src + gs.day) % c.quests.size();
    const QuestDef& qd = c.quests[q];
    gs.activeQuest  = q;
    gs.questFaction = AudienceFaction(gs) >= 0 ? AudienceFaction(gs)
                                               : gs.towns[src].owner;
    gs.questTown    = -1;
    gs.questProgress = 0;
    gs.questDays     = (float)qd.days;   // the giver's clock starts (V59)
    if (qd.type == QuestType::DeliverGrain || qd.type == QuestType::Escort) {
        // Deliver/escort to the nearest settlement you can actually walk into.
        float bestD = 1e9f;
        for (int t = 0; t < (int)gs.towns.size(); ++t) {
            if (t == src) continue;
            if (AtWar(gs, gs.towns[t].owner, c.playerFaction)) continue;
            const float d = Vector2Distance(gs.towns[src].pos,
                                            gs.towns[t].pos);
            if (d < bestD) { bestD = d; gs.questTown = t; }
        }
        if (gs.questTown < 0) {
            gs.activeQuest = -1;   // nowhere to deliver: no quest today
            return "No work today.";
        }
    }
    if (qd.type == QuestType::Escort) {
        // A REAL convoy rolls out of the gate (V69): the travellers' wagons,
        // three guards, cargo it actually buys — bandit prey unless you ride.
        const int trav = c.factions.find("travellers");
        Party wag;
        wag.faction = trav >= 0 ? trav : gs.towns[src].owner;
        wag.pos = wag.wanderTarget = gs.towns[src].pos;
        wag.caravan   = true;
        wag.caravanTo = gs.questTown;
        wag.troopCounts.assign(c.troops.size(), 0);
        if (wag.faction >= 0 && !c.factions[wag.faction].roster.empty())
            wag.troopCounts[c.factions[wag.faction].roster[0]] = 3;   // TODO(balance)
        gs.parties.push_back(wag);
        gs.questEscort = (int)gs.parties.size() - 1;
    }
    const std::string msg =
        qd.type == QuestType::Escort
            ? TextFormat("%s: %s  See the convoy safe to %s.", qd.name.c_str(),
                         qd.blurb.c_str(), gs.towns[gs.questTown].name.c_str())
        : gs.questTown >= 0
            ? TextFormat("%s: %s  Bring %d grain to %s.", qd.name.c_str(),
                         qd.blurb.c_str(), qd.amount,
                         gs.towns[gs.questTown].name.c_str())
            : TextFormat("%s: %s  (%d gold)", qd.name.c_str(), qd.blurb.c_str(),
                         qd.goldReward);
    gs.resultText = msg;
    // The journal opens a page (V124).
    gs.questLog.insert(gs.questLog.begin(),
                       TextFormat("Day %d — TAKEN: %s", gs.day, qd.name.c_str()));
    if (gs.questLog.size() > 20) gs.questLog.pop_back();
    return msg;
}

// Keep a point out of every building footprint (simple AABB push-out).
Vector3 CollideBuildings(Vector3 p, float radius) {
    for (const Building& b : T.buildings) {
        const float hx = b.size.x * 0.5f + radius;
        const float hz = b.size.z * 0.5f + radius;
        const float dx = p.x - b.pos.x;
        const float dz = p.z - b.pos.z;
        if (fabsf(dx) >= hx || fabsf(dz) >= hz) continue;
        // push out along the shallower axis
        if (hx - fabsf(dx) < hz - fabsf(dz))
            p.x = b.pos.x + (dx < 0 ? -hx : hx);
        else
            p.z = b.pos.z + (dz < 0 ? -hz : hz);
    }
    p.x = Clamp(p.x, -TOWN_EDGE, TOWN_EDGE);
    p.z = Clamp(p.z, -TOWN_EDGE, TOWN_EDGE);
    return p;
}

}  // namespace

void TownInit(const GameState& gs) {
    T = TownScene{};
    if (gs.currentSettlement < 0 || gs.currentSettlement >= (int)gs.towns.size()) return;
    const Town& town = gs.towns[gs.currentSettlement];
    const Content& c = gs.content;

    Rng rng{ (unsigned)(town.pos.x * 73856093) ^ (unsigned)(town.pos.y * 19349663) ^ 7u };

    // Castles are built, not grown: curtain walls with corner towers around a
    // courtyard, and a central keep where the lord holds court (recruiting).
    if (town.type == SettlementType::Castle) {
        const Color stone = Color{ 158, 156, 160, 255 };
        const Color dark  = Color{ 120, 118, 124, 255 };
        const float C = 26.0f;   // courtyard half-size
        auto wall = [&](Vector3 pos, Vector3 size) {
            Building b;
            b.pos = pos; b.size = size;
            b.wall = stone; b.roof = dark; b.flatTop = true;
            T.buildings.push_back(b);
        };
        // north, east, west full walls; south wall split by the gate
        wall({ 0, 0, -C }, { C * 2 + 4, 7, 3 });
        wall({ -C, 0, 0 }, { 3, 7, C * 2 + 4 });
        wall({  C, 0, 0 }, { 3, 7, C * 2 + 4 });
        wall({ -(C * 0.5f + 4.5f), 0, C }, { C - 9, 7, 3 });
        wall({  (C * 0.5f + 4.5f), 0, C }, { C - 9, 7, 3 });
        for (const float tx : { -C, C })              // corner towers
            for (const float tz : { -C, C })
                wall({ tx, 0, tz }, { 7, 11, 7 });

        Building keep;                                 // the keep, centre-north
        keep.pos = { 0, 0, -C * 0.45f };
        keep.size = { 15, 13, 13 };
        keep.wall = stone; keep.roof = GOLD;
        keep.tavern = true;                            // recruit in the hall
        T.buildings.push_back(keep);
        T.tavern = (int)T.buildings.size() - 1;

        // The garrison drills in the yard: armoured guards, not villagers.
        const int a_mail   = c.armor.find("mail");
        const int a_helm   = c.armor.find("helmet");
        const int a_bootsG = c.armor.find("boots");
        const std::vector<std::string> gLines = GatherLines(gs, /*guards=*/true);
        for (int i = 0; i < 5; ++i) {
            Npc n;
            n.pos = { rng.range(-14, 14), 0, rng.range(2, 18) };
            n.target = { rng.range(-14, 14), rng.range(2, 18) };
            n.loadout.set(EquipSlot::Body, a_mail);
            n.loadout.set(EquipSlot::Head, a_helm);
            n.loadout.set(EquipSlot::Feet, a_bootsG);
            n.line = gLines[(size_t)(rng.unit() * 0.999f * gLines.size())];
            T.npcs.push_back(n);
        }

        T.pPos = { 0, 0, C + 8 };   // start outside the gate...
        T.yaw  = PI;                // ...facing in through it
        if (IsWindowReady()) DisableCursor();
        T.hasLastMouse = false;
        return;
    }

    // Buildings ring a central plaza; count scales with settlement type
    // (identity, not balance).
    int count = 8;
    if (town.type == SettlementType::Village) count = 5;
    for (int i = 0; i < count; ++i) {
        Building b;
        const float ang = (2.0f * PI * i) / count + rng.range(-0.15f, 0.15f);
        const float rad = rng.range(20.0f, 34.0f);
        b.pos  = { cosf(ang) * rad, 0, sinf(ang) * rad };
        b.size = { rng.range(6, 11), rng.range(4, 7), rng.range(6, 11) };
        const unsigned char wallTone = (unsigned char)rng.range(140, 200);
        b.wall = town.type == SettlementType::Castle
                     ? Color{ wallTone, wallTone, wallTone, 255 }             // stone
                     : Color{ wallTone, (unsigned char)(wallTone * 0.8f),
                              (unsigned char)(wallTone * 0.55f), 255 };       // timber
        b.roof = Color{ (unsigned char)rng.range(120, 170), 60, 50, 255 };
        T.buildings.push_back(b);
    }
    T.tavern = 0;                      // first building is the tavern
    T.buildings[0].tavern = true;
    T.buildings[0].roof = GOLD;        // gilt roof so it reads from the street
    T.stalls = true;                   // trade spills onto the plaza (N5)

    // Villagers: unarmed folk in simple clothes drifting between spots.
    const int a_tunic = c.armor.find("tunic");
    const int a_boots = c.armor.find("boots");
    const int a_cap   = c.armor.find("cap");
    int folk = 10;
    if (town.type == SettlementType::Village) folk = 6;
    if (town.type == SettlementType::Castle)  folk = 6;
    const std::vector<std::string> vLines = GatherLines(gs, /*guards=*/false);
    for (int i = 0; i < folk; ++i) {
        Npc n;
        n.pos = { rng.range(-16, 16), 0, rng.range(-16, 16) };
        n.target = { rng.range(-20, 20), rng.range(-20, 20) };
        n.loadout.set(EquipSlot::Body, a_tunic);
        n.loadout.set(EquipSlot::Feet, a_boots);
        if (rng.unit() > 0.5f) n.loadout.set(EquipSlot::Head, a_cap);
        n.line = vLines[(size_t)(rng.unit() * 0.999f * vLines.size())];
        T.npcs.push_back(n);
    }

    T.pPos = { 0, 0, -14 };            // start at the plaza's south edge
    T.menu = true;                     // arrive at the gate menu (U4)
    if (IsWindowReady()) EnableCursor();   // the menu is mouse-driven
    T.hasLastMouse = false;
}

// Whether the settlement menu is up (U4) — the input gatherer needs to know
// so number keys mean menu rows, not tavern recruits.
bool TownInMenu() { return T.menu; }

bool TownUpdate(GameState& gs, float dt, const BattleInput& in, const CampaignInput& cinRaw) {
    if (dt > 0.05f) dt = 0.05f;
    const Content& c = gs.content;

    // The settlement menu (U4): rows translate into the same intents the
    // hotkeys raise, so every action below has exactly one implementation.
    CampaignInput cin = cinRaw;
    if (T.menu) {
        switch (cinRaw.menuChoice) {
            case 1: cin.openMarket = true; break;
            case 2:   // the tavern: walk in at the hearth
                TownGoTavern(gs);
                T.menu = false;
                if (IsWindowReady()) DisableCursor();
                break;
            case 3: cin.tournament = true; break;
            case 4: cin.quest = true; break;
            case 5: cin.hire = true; break;
            case 6: cin.swear = true; break;
            case 7:   // the hall: court at a castle, else the local talk
                TownTalkLord(gs);
                gs.screen = Screen::Dialogue;
                return true;
            case 8: cin.garrisonOne = true; break;
            case 9: cin.ungarrisonOne = true; break;
            case 10:   // boots on the ground
                T.menu = false;
                if (IsWindowReady()) DisableCursor();
                break;
            case 11: {   // host a feast in your own hall (V34)
                constexpr int   FEAST_COST = 200;    // TODO(balance)
                constexpr float FEAST_LEN  = 2.0f;   // days. TODO(balance)
                Town& t = gs.towns[gs.currentSettlement];
                if (t.owner != c.playerFaction)
                    gs.resultText = "Feasts are held in your own hall.";
                else if (gs.feastDays > 0)
                    gs.resultText = "A feast is already laid somewhere in the land.";
                else if (gs.gold < FEAST_COST)
                    gs.resultText = TextFormat("A feast worth the name costs %d gold.",
                                               FEAST_COST);
                else {
                    gs.gold -= FEAST_COST;
                    gs.feastTown    = gs.currentSettlement;
                    gs.feastFaction = c.playerFaction;
                    gs.feastDays    = FEAST_LEN;
                    gs.feastGuests.clear();   // fresh tables (V38)
                    int lords = 0;
                    for (const Party& p : gs.parties) {
                        if (!p.alive || p.faction != c.playerFaction ||
                            p.lord.empty()) continue;
                        LordOpinion(gs, p.lord) += 8;   // TODO(balance)
                        lords++;
                    }
                    t.prosperity += 2;   // the town eats well too. TODO(balance)
                    gs.resultText = TextFormat(
                        "The hall is laid! %d lord(s) raise a cup to you. "
                        "Matches may be made while the feast holds.", lords);
                    Chronicle(gs, TextFormat("A feast held at %s.", t.name.c_str()));
                    SfxPlay(Sfx::Fanfare);
                }
                break;
            }
            case 12: {   // sellswords in the taproom (V47)
                constexpr int PACK_COST = 150, PACK_SIZE = 5;   // TODO(balance)
                const Town& t = gs.towns[gs.currentSettlement];
                if (t.type != SettlementType::Town)
                    gs.resultText = "Sellswords drink in towns, not here.";
                else {
                    // Whoever is in the taproom this week: the pack rotates
                    // through the sellsword trades by town and day.
                    static const char* PACKS[] = { "pikeman", "arbalist", "infantry" };
                    const int pick = (gs.currentSettlement + gs.day) % 3;
                    const int th   = c.troops.find(PACKS[pick]);
                    const int room = PartyCap(gs) - gs.player.totalTroops();
                    if (th < 0)
                        gs.resultText = "The taproom is empty tonight.";
                    else if (room < PACK_SIZE)
                        gs.resultText = TextFormat(
                            "The pack is five men. You have room for %d.", std::max(0, room));
                    else if (gs.gold < PACK_COST)
                        gs.resultText = TextFormat(
                            "Sellswords take coin up front: %d gold.", PACK_COST);
                    else {
                        gs.gold -= PACK_COST;
                        if ((int)gs.player.troopCounts.size() <= th)
                            gs.player.troopCounts.resize(c.troops.size(), 0);
                        gs.player.troopCounts[th] += PACK_SIZE;
                        gs.resultText = TextFormat(
                            "A pack of five (%s) drain their cups and fall in. (-%d gold)",
                            c.troops[th].name.c_str(), PACK_COST);
                        SfxPlay(Sfx::Fanfare);
                    }
                }
                break;
            }
            case 13: {   // fortify the walls (V51)
                constexpr int FORT_COST = 500;   // TODO(balance)
                Town& t = gs.towns[gs.currentSettlement];
                if (t.owner != c.playerFaction)
                    gs.resultText = "You may only fortify your own walls.";
                else if (t.fortified)
                    gs.resultText = "These walls are already fortified.";
                else if (gs.gold < FORT_COST)
                    gs.resultText = TextFormat("Stone and masons cost %d gold.", FORT_COST);
                else {
                    gs.gold -= FORT_COST;
                    t.fortified = true;
                    gs.resultText = TextFormat(
                        "%s IS FORTIFIED: +10 garrison beds, walls that bite back.",
                        t.name.c_str());
                    Chronicle(gs, TextFormat("%s fortified.", t.name.c_str()));
                    SfxPlay(Sfx::Fanfare);
                }
                break;
            }
            default: break;
        }
    }

    // ---- leave ----
    // Esc while walking returns to the gate menu (windowed); from the menu
    // (or headless, where scripts expect one hop) it leaves outright.
    if (cin.leaveSettlement) {
        if (!T.menu && IsWindowReady()) {
            T.menu = true;
            EnableCursor();
            return true;
        }
        gs.currentSettlement = -1;
        gs.screen = Screen::Campaign;
        if (IsWindowReady()) EnableCursor();
        return false;
    }

    // ---- raise a lord for your crown (L) — player kingdom (F3) ----
    // A crowned ruler musters a vassal host at any settlement they hold; the
    // new lord marches, sieges and respawns via the ordinary lord AI.
    if (cin.raiseLord && gs.crowned &&
        gs.towns[gs.currentSettlement].owner == c.playerFaction) {
        constexpr int RAISE_LORD_COST = 300;   // TODO(balance)
        const std::vector<std::string>& names = c.map.lordNames;   // moddable (K8)
        int myLords = 0;
        for (const Party& p : gs.parties)
            if (p.alive && p.faction == c.playerFaction && !p.lord.empty()) myLords++;
        if (gs.gold >= RAISE_LORD_COST && myLords < (int)names.size()) {
            gs.gold -= RAISE_LORD_COST;
            Party p;   // mirrors campaign's MakeLordParty (module-local there)
            p.faction = c.playerFaction;
            p.lord = names[myLords % (int)names.size()];
            p.pos = p.wanderTarget = gs.towns[gs.currentSettlement].pos;
            p.troopCounts.assign(c.troops.size(), 0);
            const std::vector<int>& roster = c.factions[c.playerFaction].roster;
            for (int i = 0; i < c.factions[c.playerFaction].lordPartySize &&
                            !roster.empty(); ++i)
                p.troopCounts[roster[i % (int)roster.size()]]++;
            gs.parties.push_back(p);
            gs.resultText = TextFormat("Lord %s raises your banner with %d men.",
                                       p.lord.c_str(), p.totalTroops());
            SfxPlay(Sfx::Fanfare);
        }
    }

    // ---- hire the tavern's companion (H) — unique heroes for hire (H1) ----
    // Each settlement's tavern hosts one companion (rotating by index); a
    // hero already riding with you cannot be hired twice.
    if (cin.hire) {
        std::vector<int> comps;
        for (int t = 0; t < c.troops.size(); ++t)
            if (c.troops[t].companion) comps.push_back(t);
        if (!comps.empty()) {
            const int h = comps[gs.currentSettlement % (int)comps.size()];
            const TroopDef& td = c.troops[h];
            if (gs.player.troopCounts[h] == 0 && gs.gold >= td.cost) {
                gs.gold -= td.cost;
                gs.player.troopCounts[h] = 1;
                gs.resultText = TextFormat("%s takes your coin and your banner.",
                                           td.name.c_str());
                SfxPlay(Sfx::Click);
            }
        }
    }

    // ---- ask the local giver for work (G) — one quest at a time (F4) ----
    // Follow-up: gate on a guild-master NPC instead of anywhere in town.
    if (cin.quest) TryQuest(gs);

    // ---- swear fealty to this settlement's crown (V; also at court, K2) ----
    if (cin.swear && gs.liege < 0) TrySwear(gs);

    // ---- enter the tournament bracket (T, towns only; Shift+T stakes gold) ----
    if (cin.tournament &&
        gs.towns[gs.currentSettlement].type == SettlementType::Town) {
        gs.arenaFight = true;
        gs.arenaRound = 1;
        gs.arenaBet   = 0;
        constexpr int ARENA_STAKE = 50;   // TODO(balance): pays 3x as champion
        if (cin.tournamentBet && gs.gold >= ARENA_STAKE) {
            gs.gold -= ARENA_STAKE;
            gs.arenaBet = ARENA_STAKE;
        }
        gs.screen = Screen::Battle;
        if (IsWindowReady()) EnableCursor();
        return false;
    }

    // ---- browse the market stalls (M) ----
    if (cin.openMarket) {
        gs.screen = Screen::Market;
        if (IsWindowReady()) EnableCursor();
        return false;
    }

    // ---- step through the tavern door (E), or back out again ----
    // In a castle the keep door leads to the lord's court instead (K2).
    if (cin.interact && (T.inside || TownAtTavern())) {
        if (!T.inside &&
            gs.towns[gs.currentSettlement].type == SettlementType::Castle) {
            TownTalkLord(gs);
            gs.screen = Screen::Dialogue;
            if (IsWindowReady()) EnableCursor();
        } else {
            T.inside = !T.inside;
            if (T.inside) { T.iPos = { 0, 0, 3.5f }; T.yaw = PI; }
        }
    } else if (cin.interact) {
        // ---- or stop a passer-by for a word (H4) ----
        for (const Npc& n : T.npcs) {
            const float dx = T.pPos.x - n.pos.x, dz = T.pPos.z - n.pos.z;
            if (dx * dx + dz * dz < 3.5f * 3.5f) {
                TownTalkNearest(gs);
                gs.screen = Screen::Dialogue;
                if (IsWindowReady()) EnableCursor();
                break;
            }
        }
    }

    // ---- hero movement (battle-style third person) ----
    // Frozen while the gate menu is up (U4): the cursor belongs to the
    // rows — but every intent handler below still runs (the menu raises
    // the same intents; the harness injects them directly).
    if (!T.menu) {
        T.yaw   -= in.lookDelta.x * 0.003f;
        T.pitch  = Clamp(T.pitch - in.lookDelta.y * 0.003f, -0.4f, 0.6f);
        const Vector3 fwd   = { sinf(T.yaw), 0, cosf(T.yaw) };
        const Vector3 right = { -fwd.z, 0, fwd.x };
        Vector3 move = Vector3Add(Vector3Scale(fwd, in.moveForward),
                                  Vector3Scale(right, in.moveRight));
        if (T.inside) {
            if (Vector3Length(move) > 0) {
                T.iPos = Vector3Add(T.iPos, Vector3Scale(Vector3Normalize(move),
                                                         WALK_SPEED * 0.7f * dt));
                T.walkPhase += dt * 10.0f;
            }
            T.iPos.x = Clamp(T.iPos.x, -5.0f, 5.0f);   // the common room
            T.iPos.z = Clamp(T.iPos.z, -3.5f, 4.2f);
        } else if (Vector3Length(move) > 0) {
            T.pPos = Vector3Add(T.pPos, Vector3Scale(Vector3Normalize(move), WALK_SPEED * dt));
            T.walkPhase += dt * 10.0f;
        }
        if (!T.inside) T.pPos = CollideBuildings(T.pPos, 0.5f);
    }

    // ---- tavern recruiting ----
    if (TownAtTavern() && cin.recruitSlot >= 0) {
        const std::vector<int>& roster = c.factions[c.playerFaction].roster;
        if (cin.recruitSlot < (int)roster.size()) {
            const TroopDef& td = c.troops[roster[cin.recruitSlot]];
            Town& tt = gs.towns[gs.currentSettlement];
            if (gs.player.totalTroops() >= PartyCap(gs)) {
                gs.resultText = TextFormat(
                    "Your name only carries %d men. Win renown for more.",
                    PartyCap(gs));
            } else if (tt.recruitPool <= 0) {   // the land is drained (V2)
                gs.resultText = TextFormat(
                    "%s has no more sons to give. Let it prosper a while.",
                    tt.name.c_str());
            } else if (gs.gold >= td.cost) {
                gs.gold -= td.cost;
                tt.recruitPool--;
                gs.player.troopCounts[roster[cin.recruitSlot]]++;
                SfxPlay(Sfx::Click);
            }
        }
    }

    // ---- garrison your walls (S2): F leaves a soldier, Shift+F recalls ----
    // Your own settlements only. Deposits take from your fullest line
    // (companions never garrison); recalls respect the party cap.
    if ((cin.garrisonOne || cin.ungarrisonOne) &&
        gs.towns[gs.currentSettlement].owner == c.playerFaction) {
        Town& t = gs.towns[gs.currentSettlement];
        if ((int)t.garrison.size() < c.troops.size())
            t.garrison.assign(c.troops.size(), 0);
        if (cin.garrisonOne) {
            int best = -1;
            for (int tr = 0; tr < (int)gs.player.troopCounts.size() &&
                             tr < c.troops.size(); ++tr) {
                if (c.troops[tr].companion) continue;
                if (best < 0 || gs.player.troopCounts[tr] >
                                gs.player.troopCounts[best])
                    best = tr;
            }
            if (best >= 0 && gs.player.troopCounts[best] > 0) {
                gs.player.troopCounts[best]--;
                t.garrison[best]++;
                gs.resultText = TextFormat("A %s joins the %s garrison (%d).",
                                           c.troops[best].name.c_str(),
                                           t.name.c_str(), t.garrisonSize());
            }
        } else if (gs.player.totalTroops() < PartyCap(gs)) {
            for (int tr = 0; tr < (int)t.garrison.size() &&
                             tr < c.troops.size(); ++tr)
                if (t.garrison[tr] > 0) {
                    t.garrison[tr]--;
                    gs.player.troopCounts[tr]++;
                    gs.resultText = TextFormat(
                        "A %s comes down off the wall (garrison %d).",
                        c.troops[tr].name.c_str(), t.garrisonSize());
                    break;
                }
        }
    }

    // ---- prisoner lords (O2): sell them back, or set them free ----
    // Ransom pays 200 a head and the lord resents the ledger (-10); release
    // pays nothing and is remembered kindly (+20 lord, +5 crown, +2 honor).
    // Either way he rides again (respawn queued). TODO(balance): all of it.
    if ((cin.ransomLords || cin.releaseLords) && !gs.capturedLords.empty()) {
        std::string names;
        for (const auto& pl : gs.capturedLords) {
            if (!names.empty()) names += ", ";
            names += pl.first;
            if (cin.ransomLords) {
                gs.gold += 200;
                LordOpinion(gs, pl.first) -= 10;
            } else {
                gs.honor += 2;
                LordOpinion(gs, pl.first) += 20;
                NudgeRelation(gs, pl.second, +5);
            }
            gs.lordRespawns.push_back({ pl.second, pl.first, 10.0f });
        }
        gs.resultText = cin.ransomLords
            ? TextFormat("Ransomed to their crowns: %s.  The gold is cold.",
                         names.c_str())
            : TextFormat("You free %s. Such things are not forgotten.",
                         names.c_str());
        gs.capturedLords.clear();
        SfxPlay(Sfx::Fanfare);
    }

    // ---- ransom captives (flat gold a head; TODO(balance)) ----
    if (TownAtTavern() && cin.ransom) {
        int heads = 0;
        for (int& n : gs.prisoners) { heads += n; n = 0; }
        if (heads > 0) {
            // A jailer haggles like he guards: hard (V78). TODO(balance).
            const int perHead = HasPerk(gs, "jailer") ? 15 : 10;
            const int gold = heads * perHead;
            gs.gold += gold;
            gs.resultText = TextFormat("Ransomed %d captives for %d gold.%s",
                                       heads, gold,
                                       perHead > 10 ? "  (Hodd drove the price)" : "");
        }
    }

    // ---- villagers drift about ----
    for (Npc& n : T.npcs) {
        n.think -= dt;
        const Vector2 to = { n.target.x - n.pos.x, n.target.y - n.pos.z };
        const float d = sqrtf(to.x * to.x + to.y * to.y);
        if (d < 1.0f || n.think <= 0) {
            n.target = { (float)GetRandomValue(-22, 22), (float)GetRandomValue(-22, 22) };
            n.think = (float)GetRandomValue(4, 10);
        } else {
            n.yaw = atan2f(to.x, to.y);
            n.pos.x += (to.x / d) * NPC_SPEED * dt;
            n.pos.z += (to.y / d) * NPC_SPEED * dt;
            n.walkPhase += dt * 6.0f;
        }
        n.pos = CollideBuildings(n.pos, 0.4f);
    }
    return true;
}

TownView GetTownView() {
    TownView v;
    v.heroPos = T.pPos;
    v.heroYaw = T.yaw;
    if (T.tavern >= 0 && T.tavern < (int)T.buildings.size())
        v.tavernPos = T.buildings[T.tavern].pos;
    v.npcs = (int)T.npcs.size();
    v.atTavern = TownAtTavern();
    v.inside = T.inside;
    return v;
}

void TownTalkLord(GameState& gs) {
    const Content& c = gs.content;
    const int owner = gs.towns[gs.currentSettlement].owner;
    gs.audienceLord.clear();
    if (owner == c.playerFaction)
        gs.dialogueName = "Your Castellan";
    else if (!gs.towns[gs.currentSettlement].fiefLord.empty())   // the seat's own
        gs.audienceLord = gs.towns[gs.currentSettlement].fiefLord;   // lord (S4)
    else if (c.factions.valid(owner) && !c.factions[owner].lords.empty())
        gs.audienceLord = c.factions[owner].lords[0];
    else
        gs.dialogueName = "The Castellan";
    if (!gs.audienceLord.empty())
        gs.dialogueName = TextFormat("Lord %s", gs.audienceLord.c_str());
    gs.dialogueLord = true;
    gs.dialogueLines.clear();
    gs.dialogueLines.push_back("Speak, captain. The court listens.");
}

// Walk (well, appear) at the tavern door — the harness's legs (Q1). The
// windowed player walks there; scripts should not have to steer a scene.
void TownGoTavern(GameState& gs) {
    (void)gs;
    if (T.tavern >= 0 && T.tavern < (int)T.buildings.size()) {
        const Building& b = T.buildings[T.tavern];
        T.pPos = { b.pos.x, 0, b.pos.z + b.size.z * 0.5f + 2.0f };
    }
}

void TownTalkNearest(GameState& gs) {
    const Npc* best = nullptr;
    float bestD = 1e9f;
    for (const Npc& n : T.npcs) {
        const float dx = T.pPos.x - n.pos.x, dz = T.pPos.z - n.pos.z;
        const float d = dx * dx + dz * dz;
        if (d < bestD) { bestD = d; best = &n; }
    }
    const bool castle = gs.currentSettlement >= 0 &&
                        gs.currentSettlement < (int)gs.towns.size() &&
                        gs.towns[gs.currentSettlement].type == SettlementType::Castle;
    gs.dialogueName = castle ? "Guardsman" : "Villager";
    gs.dialogueLord = false;
    gs.audienceLord.clear();
    gs.dialogueLines.clear();
    gs.dialogueLines.push_back(best ? best->line : "Well met, captain.");

    // Rumors that know the world (V70): the small folk repeat what is
    // actually happening — wars, feasts, the storm, a bargain on the
    // shelves — rotated by the day so the street stays worth asking.
    {
        const Content& c = gs.content;
        std::vector<std::string> rumors;
        const int nf = c.factions.size();
        for (int a = 0; a < nf && rumors.size() < 6; ++a)
            for (int b = a + 1; b < nf; ++b)
                if (c.factions[a].kingdom && c.factions[b].kingdom &&
                    AtWar(gs, a, b)) {
                    rumors.push_back(TextFormat(
                        "They say %s and %s are at each other's throats.",
                        c.factions[a].name.c_str(), c.factions[b].name.c_str()));
                    break;
                }
        if (gs.feastDays > 0 && gs.feastTown >= 0)
            rumors.push_back(TextFormat("There's feasting at %s - all at peace are welcome.",
                                        gs.towns[gs.feastTown].name.c_str()));
        {   // where the storm sits, roughly
            int nearT = -1; float bd = 1e9f;
            for (int t = 0; t < (int)gs.towns.size(); ++t) {
                const float d = Vector2Distance(gs.stormPos, gs.towns[t].pos);
                if (d < bd) { bd = d; nearT = t; }
            }
            if (nearT >= 0)
                rumors.push_back(TextFormat("Foul weather over %s way, they say.",
                                            gs.towns[nearT].name.c_str()));
        }
        if (gs.currentSettlement >= 0) {   // the local bargain
            const Town& t = gs.towns[gs.currentSettlement];
            for (int g = 0; g < c.goods.size() && g < (int)t.priceOffset.size(); ++g)
                if (t.priceOffset[g] < 100) {
                    rumors.push_back(TextFormat("%s is cheap here - traders buy it by the cart.",
                                                c.goods[g].name.c_str()));
                    break;
                }
        }
        if (!rumors.empty())
            gs.dialogueLines.push_back(rumors[gs.day % (int)rumors.size()]);
    }
}

// Grant the seat you stand in to a raised lord without one (M3). Returns the
// court's answer — shared by the dialogue topic and any future hotkey.
std::string TryGrantFief(GameState& gs) {
    const Content& c = gs.content;
    if (gs.currentSettlement < 0)   // a road parley (S4) grants nothing
        return "Seats are granted from their own halls.";
    Town& t = gs.towns[gs.currentSettlement];
    if (!gs.crowned) return "Only a crowned ruler grants fiefs.";
    if (t.owner != c.playerFaction) return "This seat is not yours to give.";
    if (!t.fiefLord.empty())
        return TextFormat("Lord %s already holds this seat.", t.fiefLord.c_str());
    for (const Party& p : gs.parties) {
        if (!p.alive || p.faction != c.playerFaction || p.lord.empty()) continue;
        bool landed = false;
        for (const Town& o : gs.towns)
            if (o.fiefLord == p.lord) { landed = true; break; }
        if (landed) continue;
        t.fiefLord = p.lord;
        gs.honor += 1;   // generosity becomes a ruler (M3). TODO(balance)
        LordOpinion(gs, p.lord) += 20;   // land is remembered (N4)
        gs.resultText = TextFormat("%s is granted to Lord %s. He will hold it.",
                                   t.name.c_str(), p.lord.c_str());
        SfxPlay(Sfx::Fanfare);
        return gs.resultText;
    }
    return "No landless lord attends your court. Raise one first (L).";
}

// Rebellion (O6): a sworn vassal declares the crown should be theirs. Needs
// a great name and lords willing to follow; the willing defect, the crown
// answers with war, and the civil war begins. TODO(balance): every gate.
std::string TryRebel(GameState& gs) {
    const Content& c = gs.content;
    constexpr int RENOWN_TO_REBEL  = 15;
    constexpr int OPINION_TO_FOLLOW = 10;
    if (gs.liege < 0) return "You serve no crown. There is nothing to usurp.";
    const int crown = gs.liege;
    if (gs.renown < RENOWN_TO_REBEL)
        return TextFormat("Usurpers need a great name. Win renown first (%d/%d).",
                          gs.renown, RENOWN_TO_REBEL);
    // Count the crown's lords who would follow you.
    int willing = 0, lords = 0;
    for (const Party& p : gs.parties) {
        if (!p.alive || p.faction != crown || p.lord.empty()) continue;
        lords++;
        if (EffectiveLordOpinion(gs, p.lord) >= OPINION_TO_FOLLOW) willing++;
    }
    if (lords > 0 && willing * 2 < lords)
        return "No lord would follow you. Court them first.";
    // The die is cast: the willing defect, the crown answers with war.
    int defected = 0;
    for (Party& p : gs.parties) {
        if (!p.alive || p.faction != crown || p.lord.empty()) continue;
        if (EffectiveLordOpinion(gs, p.lord) >= OPINION_TO_FOLLOW) {
            p.faction = c.playerFaction;
            defected++;
        }
    }
    const int n = c.factions.size();
    if ((int)gs.hostile.size() == n * n)
        gs.hostile[(size_t)c.playerFaction * n + crown] =
            gs.hostile[(size_t)crown * n + c.playerFaction] = 1;
    NudgeRelation(gs, crown, -60);
    gs.liege   = -1;
    gs.crowned = true;
    gs.honor  -= 2;   // an oath broken is an oath broken
    gs.resultText = TextFormat(
        "REBELLION!  %d lord(s) declare for you. %s answers with war.",
        defected, c.factions[crown].name.c_str());
    Chronicle(gs, TextFormat("Rebellion against %s - %d lord(s) follow you.",
                             c.factions[crown].name.c_str(), defected));
    SfxPlay(Sfx::WarCry);
    return gs.resultText;
}

namespace {
// Where DialogueDraw put its topic rows this frame (V27) — windowed-only
// shared state between draw and the gather-side hit-test.
std::vector<std::pair<int, int>> g_dlgHits;   // {rowY, menuChoice}
int g_dlgHitX = 0;

// Walking-mode service chips (V122): {x, y, w, h, id} recorded by TownDraw.
struct SvcHit { int x, y, w, h, id; };
std::vector<SvcHit> g_svcHits;
}   // namespace

int TownServiceAt(Vector2 mouse) {
    for (const auto& h : g_svcHits)
        if (mouse.x >= h.x && mouse.x < h.x + h.w &&
            mouse.y >= h.y && mouse.y < h.y + h.h)
            return h.id;
    return 0;
}

int DialogueOptionAt(Vector2 mouse) {
    for (const auto& h : g_dlgHits)
        if (mouse.x >= g_dlgHitX - 8 && mouse.x < g_dlgHitX + 660 &&
            mouse.y >= h.first - 3 && mouse.y < h.first + 27)
            return h.second;
    return 0;
}

void DialogueUpdate(GameState& gs, const CampaignInput& in) {
    if (in.menuChoice == 1) {   // "What news of the war?"
        const bool castle = gs.currentSettlement >= 0 &&
                            gs.towns[gs.currentSettlement].type == SettlementType::Castle;
        gs.dialogueLines = GatherLines(gs, castle);
    } else if (in.menuChoice == 2 && gs.dialogueLord) {   // "I would swear my sword."
        gs.dialogueLines.clear();
        gs.dialogueLines.push_back(TrySwear(gs));
    } else if (in.menuChoice == 3 && gs.dialogueLord) {   // "Have you work for me?"
        gs.dialogueLines.clear();
        gs.dialogueLines.push_back(TryQuest(gs));
    } else if (in.menuChoice == 4 && gs.dialogueLord) {   // "I grant this seat." (M3)
        gs.dialogueLines.clear();
        gs.dialogueLines.push_back(TryGrantFief(gs));
    } else if (in.menuChoice == 10 && gs.dialogueLord) {   // pay Graves (V87)
        gs.dialogueLines.clear();
        if (gs.audienceLord != "Graves" || gs.debt <= 0)
            gs.dialogueLines.push_back("He has no bill with your name on it.");
        else if (gs.gold < gs.debt)
            gs.dialogueLines.push_back(TextFormat(
                "Graves counts your purse from the saddle: %d against %d owed. "
                "Not enough.", gs.gold, gs.debt));
        else {
            gs.gold -= gs.debt;
            gs.resultText = TextFormat(
                "You count out %d in the road. Graves turns his men without a word.",
                gs.debt);
            gs.debt = 0;
            gs.debtDays = 0;
            if (gs.parleyParty >= 0 && gs.parleyParty < (int)gs.parties.size())
                gs.parties[gs.parleyParty].alive = false;   // the hunt is over
            Chronicle(gs, "Paid the collectors off in the open field.");
            gs.dialogueLines.push_back(gs.resultText);
            SfxPlay(Sfx::Fanfare);
        }
    } else if (in.menuChoice == 9 && gs.dialogueLord) {   // turn his coat (V73)
        gs.dialogueLines.clear();
        const Content& c = gs.content;
        constexpr int POACH_OPINION = 20;   // TODO(balance)
        const bool road = gs.parleyParty >= 0 &&
                          gs.parleyParty < (int)gs.parties.size();
        const int  pf   = road ? gs.parties[gs.parleyParty].faction : -1;
        if (!gs.crowned)
            gs.dialogueLines.push_back("Only a crowned head may offer a crown's service.");
        else if (!road || !c.factions.valid(pf) || !c.factions[pf].kingdom ||
                 pf == c.playerFaction)
            gs.dialogueLines.push_back("He serves no crown worth stealing from.");
        else if (EffectiveLordOpinion(gs, gs.audienceLord) < POACH_OPINION)
            gs.dialogueLines.push_back(TextFormat(
                "Lord %s is not yours yet. Court him first (%+d of %+d).",
                gs.audienceLord.c_str(),
                EffectiveLordOpinion(gs, gs.audienceLord), POACH_OPINION));
        else {
            Party& p  = gs.parties[gs.parleyParty];
            p.faction = c.playerFaction;
            NudgeRelation(gs, pf, -25);   // TODO(balance): the crown he leaves
            gs.resultText = TextFormat(
                "LORD %s TURNS HIS COAT!  His host of %d flies your banner now.",
                p.lord.c_str(), p.totalTroops());
            Chronicle(gs, TextFormat("Lord %s abandons %s for your banner.",
                                     p.lord.c_str(), c.factions[pf].name.c_str()));
            gs.dialogueLines.push_back(gs.resultText);
            SfxPlay(Sfx::Fanfare);
        }
    } else if (in.menuChoice == 8 && gs.dialogueLord) {   // hire the company (V29)
        constexpr int   MERC_COST = 300;   // TODO(balance)
        constexpr float MERC_DAYS = 3.0f;  // TODO(balance)
        gs.dialogueLines.clear();
        const bool sellsword = gs.parleyParty >= 0 &&
            gs.content.factions.valid(gs.parties[gs.parleyParty].faction) &&
            gs.content.factions[gs.parties[gs.parleyParty].faction].mercenary;
        if (!sellsword)
            gs.dialogueLines.push_back("This is no sellsword. His oath is not for sale.");
        else if (gs.mercParty >= 0 && gs.mercDays > 0)
            gs.dialogueLines.push_back("You already keep a company under contract.");
        else if (gs.gold < MERC_COST)
            gs.dialogueLines.push_back(TextFormat(
                "Steel costs gold, captain. %d of it.", MERC_COST));
        else {
            gs.gold -= MERC_COST;
            gs.mercParty = gs.parleyParty;
            gs.mercDays  = MERC_DAYS;
            Chronicle(gs, TextFormat(
                "The %s taken under contract.",
                gs.content.factions[gs.parties[gs.parleyParty].faction].name.c_str()));
            gs.resultText = TextFormat(
                "The %s marches under your banner for %.0f days.",
                gs.content.factions[gs.parties[gs.parleyParty].faction].name.c_str(),
                MERC_DAYS);
            gs.dialogueLines.push_back(gs.resultText);
            SfxPlay(Sfx::Fanfare);
        }
    } else if (in.menuChoice == 7 && gs.dialogueLord) {   // court him with a gift (V26)
        constexpr int GIFT_COST = 100, GIFT_OPINION = 10;   // TODO(balance)
        gs.dialogueLines.clear();
        if (gs.audienceLord.empty())
            gs.dialogueLines.push_back("A castellan takes no gifts. His lord might.");
        else if (gs.gold < GIFT_COST)
            gs.dialogueLines.push_back("Gifts worth giving cost gold. Come back richer.");
        else {
            gs.gold -= GIFT_COST;
            LordOpinion(gs, gs.audienceLord) += GIFT_OPINION;
            gs.dialogueLines.push_back(TextFormat(
                "Lord %s accepts your gift with grace. He will remember this. (%+d)",
                gs.audienceLord.c_str(), EffectiveLordOpinion(gs, gs.audienceLord)));
            SfxPlay(Sfx::Fanfare);
        }
    } else if (in.menuChoice == 6 && gs.dialogueLord) {   // rebellion (O6)
        gs.dialogueLines.clear();
        gs.dialogueLines.push_back(TryRebel(gs));
    } else if (in.menuChoice == 5 && gs.dialogueLord) {   // marriage suit (M5)
        gs.dialogueLines.clear();
        const int host = gs.feastFaction;
        if (gs.spouseFaction >= 0)
            gs.dialogueLines.push_back("You are already wed, captain.");
        else if (gs.feastTown != gs.currentSettlement || gs.feastDays <= 0)
            gs.dialogueLines.push_back("Marriages are made at feasts. Find one.");
        else if (gs.renown < 10)   // TODO(balance): suitor's renown
            gs.dialogueLines.push_back(
                "No house weds an unknown. Win renown first (10).");
        else {
            static const char* BRIDES[] = { "Elina", "Mira", "Isolde", "Anneth" };
            gs.spouseFaction = host;
            gs.spouseName    = BRIDES[host % 4];
            NudgeRelation(gs, host, +20);   // TODO(balance)
            gs.resultText = TextFormat(
                "You are wed to Lady %s. Two houses are now one.",
                gs.spouseName.c_str());
            Chronicle(gs, gs.resultText);
            gs.dialogueLines.push_back(gs.resultText);
            SfxPlay(Sfx::Fanfare);
        }
    } else if (in.menuChoice == 2) {   // "Any work for a warband?"
        gs.dialogueLines.clear();
        gs.dialogueLines.push_back("Work? The giver posts it - ask around town (G).");
        for (const Lair& l : gs.lairs)
            if (l.alive) {
                gs.dialogueLines.push_back(TextFormat(
                    "And there's a den of cutthroats out at (%.0f, %.0f)...",
                    l.pos.x, l.pos.y));
                break;
            }
    }
    if (in.leaveSettlement) {
        if (gs.parleyParty >= 0) {   // a road parley returns to the road (S4)
            gs.parleyParty = -1;
            gs.screen = Screen::Campaign;
            gs.dialogueLines.clear();
        } else {
            gs.screen = Screen::Settlement;
            gs.dialogueLines.clear();
            if (IsWindowReady()) DisableCursor();
        }
    }
}

void DialogueDraw(const GameState& gs) {
    BeginDrawing();
    ClearBackground(Color{ 24, 26, 30, 255 });
    const int w = GetScreenWidth();
    const int x = w / 2 - 320;

    // A painted bust stands in for a portrait until faces exist.
    DrawRectangle(x, 90, 120, 150, Fade(Color{ 60, 50, 44, 255 }, 0.9f));
    DrawRectangleLines(x, 90, 120, 150, Fade(GOLD, 0.5f));
    DrawCircle(x + 60, 140, 26, Color{ 214, 176, 142, 255 });          // head
    DrawRectangle(x + 24, 170, 72, 60, Color{ 96, 84, 60, 255 });      // shoulders
    ui::Title(gs.dialogueName.c_str(), x + 140, 110, 40, GOLD);
    if (!gs.audienceLord.empty()) {   // his opinion of you, on his face (V26)
        const int op = EffectiveLordOpinion(const_cast<GameState&>(gs), gs.audienceLord);
        ui::Text(TextFormat("his opinion of you: %+d", op), x + 140, 160, 20,
                 op >= 10 ? Fade(GREEN, 0.9f) : op <= -10 ? Fade(RED, 0.9f)
                                              : Fade(RAYWHITE, 0.7f));
    }

    int y = 270;
    for (const std::string& line : gs.dialogueLines) {
        ui::Text(TextFormat("\"%s\"", line.c_str()), x, y, 22, RAYWHITE);
        y += 32;
    }

    // Topic rows are buttons (V27): each row drawn is recorded for the
    // gather-side hit-test, and the row under the mouse glows.
    g_dlgHits.clear();
    const Vector2 mp = GetMousePosition();
    int optY = y + 30;
    auto option = [&](int choice, const char* text, Color col) {
        const bool hover = mp.x >= x - 8 && mp.x < x + 660 &&
                           mp.y >= optY - 3 && mp.y < optY + 27;
        if (hover)
            DrawRectangle(x - 8, optY - 3, 660, 30, Fade(GOLD, 0.14f));
        ui::Text(text, x, optY, 22, hover ? RAYWHITE : col);
        g_dlgHits.push_back({ optY, choice });
        optY += 30;
    };
    option(1, "[1] What news of the war?", Fade(RAYWHITE, 0.85f));
    if (gs.dialogueLord) {
        option(2, "[2] I would swear my sword to this crown.", Fade(RAYWHITE, 0.85f));
        option(3, "[3] Have you work for my warband?", Fade(RAYWHITE, 0.85f));
        if (!gs.audienceLord.empty())   // court him with a gift (V26)
            option(7, "[7] A gift for your household. (100 gold)", Fade(GOLD, 0.85f));
        if (gs.parleyParty >= 0 &&      // a sellsword sells (V29)
            gs.content.factions.valid(gs.parties[gs.parleyParty].faction) &&
            gs.content.factions[gs.parties[gs.parleyParty].faction].mercenary)
            option(8, "[8] March with me. (300 gold, 3 days)", Fade(GOLD, 0.85f));
        if (gs.audienceLord == "Graves" && gs.debt > 0)   // pay the man (V87)
            option(10, TextFormat("[0] Pay what you owe. (%d gold)", gs.debt),
                   Fade(RED, 0.9f));
        if (gs.crowned && gs.parleyParty >= 0 &&   // turn his coat (V73)
            gs.content.factions.valid(gs.parties[gs.parleyParty].faction) &&
            gs.content.factions[gs.parties[gs.parleyParty].faction].kingdom &&
            gs.parties[gs.parleyParty].faction != gs.content.playerFaction)
            option(9, "[9] Abandon your crown. Serve mine.", Fade(RED, 0.85f));
        if (gs.crowned)   // a ruler's court has a ruler's business (M3)
            option(4, "[4] I grant this seat to a lord of mine.", Fade(GOLD, 0.85f));
        if (gs.currentSettlement >= 0 &&
            gs.feastTown == gs.currentSettlement && gs.feastDays > 0 &&
            gs.spouseFaction < 0)   // a feast's court makes matches (M5)
            option(5, "[5] I seek a marriage alliance.", Fade(PINK, 0.85f));
        if (gs.liege >= 0 && gs.currentSettlement >= 0 &&
            gs.towns[gs.currentSettlement].owner == gs.liege)   // O6
            option(6, "[6] The crown should be mine.", Fade(RED, 0.85f));
    } else {
        option(2, "[2] Any work for a warband?", Fade(RAYWHITE, 0.85f));
    }
    option(DLG_LEAVE, "[Esc / E] Take your leave", Fade(RAYWHITE, 0.6f));
    g_dlgHitX = x;
    EndDrawing();
}

bool TownAtTavern() {
    if (T.tavern < 0 || T.tavern >= (int)T.buildings.size()) return false;
    const Building& b = T.buildings[T.tavern];
    const float dx = T.pPos.x - b.pos.x, dz = T.pPos.z - b.pos.z;
    return sqrtf(dx * dx + dz * dz) < TAVERN_RANGE + fmaxf(b.size.x, b.size.z) * 0.5f;
}

void TownDraw(const GameState& gs) {
    const Content& c = gs.content;
    const Town& town = gs.towns[gs.currentSettlement >= 0 ? gs.currentSettlement : 0];

    // ---- inside the tavern: a lamplit common room ----
    if (T.inside) {
        SfxAmbience(0.05f);
        Camera3D cam = { 0 };
        const Vector3 look = { sinf(T.yaw) * cosf(T.pitch), sinf(T.pitch),
                               cosf(T.yaw) * cosf(T.pitch) };
        const Vector3 eye = { T.iPos.x, T.iPos.y + 1.9f, T.iPos.z };
        cam.position = Vector3Subtract(eye, Vector3Scale(look, 2.4f));
        cam.position.y = Clamp(cam.position.y, 0.5f, 3.6f);
        cam.position.x = Clamp(cam.position.x, -5.6f, 5.6f);   // stay in the room
        cam.position.z = Clamp(cam.position.z, -4.1f, 4.5f);
        cam.target = Vector3Add(eye, Vector3Scale(look, 3.0f));
        cam.up = { 0, 1, 0 };
        cam.fovy = 60;
        cam.projection = CAMERA_PERSPECTIVE;

        BeginDrawing();
        ClearBackground(Color{ 24, 18, 14, 255 });
        BeginMode3D(cam);
        BeginShaderMode(GetLitShader());
        const Color wood = { 92, 66, 44, 255 };
        const Color dark = { 58, 42, 30, 255 };
        DrawPlane({ 0, 0, 0 }, { 12, 10 }, dark);                       // floor
        DrawCube({ 0, 2.0f, -4.6f }, 12, 4, 0.4f, wood);                // walls
        DrawCube({ 0, 2.0f, 5.0f }, 12, 4, 0.4f, wood);
        DrawCube({ -6.2f, 2.0f, 0 }, 0.4f, 4, 10, wood);
        DrawCube({ 6.2f, 2.0f, 0 }, 0.4f, 4, 10, wood);
        DrawCube({ 0, 4.1f, 0 }, 12, 0.3f, 10, dark);                   // ceiling
        // hearth on the west wall
        DrawCube({ -5.8f, 1.0f, -2.0f }, 0.8f, 2.0f, 2.0f, Color{ 70, 66, 64, 255 });
        DrawCube({ -5.5f, 0.6f, -2.0f }, 0.5f, 0.9f, 1.2f, Color{ 240, 140, 40, 255 });
        DrawSphere({ -5.2f, 0.8f, -2.0f }, 0.9f, Fade(ORANGE, 0.18f));  // glow
        // the counter and the keeper behind it
        DrawCube({ 3.0f, 0.7f, -3.2f }, 4.5f, 1.4f, 0.8f, wood);
        Pose keeper;
        keeper.yaw = 0.2f;
        DrawCharacter(c, { 3.0f, 0, -4.1f }, T.npcs.empty() ? Loadout{} : T.npcs[0].loadout,
                      keeper, BEIGE);
        // tables with kegs, and a couple of patrons
        for (const float tx : { -2.0f, 1.0f }) {
            DrawCube({ tx, 0.55f, 1.2f }, 1.6f, 1.1f, 1.6f, wood);
            DrawCylinder({ tx + 0.3f, 1.1f, 1.2f }, 0.16f, 0.16f, 0.3f, 8,
                         Color{ 120, 90, 60, 255 });
            Pose sit;
            sit.yaw = tx < 0 ? 1.2f : -1.6f;
            DrawCharacter(c, { tx + (tx < 0 ? -1.0f : 1.0f), 0, 1.2f },
                          T.npcs.size() > 1 ? T.npcs[1].loadout : Loadout{}, sit, BEIGE);
        }
        // the hero
        DrawCylinder({ T.iPos.x, 0.03f, T.iPos.z }, 0.5f, 0.5f, 0.02f, 12, Fade(BLACK, 0.3f));
        Pose hero;
        hero.yaw = T.yaw;
        hero.walkPhase = T.walkPhase;
        DrawCharacter(c, T.iPos, gs.playerHero.loadout, hero, Color{ 40, 120, 255, 255 });
        EndShaderMode();
        EndMode3D();

        SfxMinstrel(0.3f);   // the minstrel plays by the hearth (N5)
        // HUD: same tavern business, indoors where it belongs.
        DrawRectangle(0, 0, GetScreenWidth(), 34, Fade(BLACK, 0.6f));
        ui::Text(TextFormat("The %s tavern  ·  Gold: %d   Party: %d", town.name.c_str(),
                            gs.gold, gs.player.totalTroops()), 10, 8, 20, RAYWHITE);
        const std::vector<int>& roster = c.factions[c.playerFaction].roster;
        int captives = 0;
        for (int n : gs.prisoners) captives += n;
        const int y = GetScreenHeight() - 60 - (int)roster.size() * 24 - (captives > 0 ? 24 : 0);
        DrawRectangle(0, y - 8, GetScreenWidth(), GetScreenHeight() - y + 8, Fade(BLACK, 0.7f));
        ui::Text("Recruits drink in the corner:", 10, y, 20, GOLD);
        for (int slot = 0; slot < (int)roster.size(); ++slot) {
            const TroopDef& td = c.troops[roster[slot]];
            ui::Text(TextFormat("[%d] %s - %d gold  (have %d)", slot + 1, td.name.c_str(),
                                td.cost, gs.player.troopCounts[roster[slot]]),
                     10, y + 26 + slot * 24, 20, RAYWHITE);
        }
        if (captives > 0)
            ui::Text(TextFormat("[R] Ransom %d captives (%d gold)", captives, captives * 10),
                     10, GetScreenHeight() - 54, 20, LIME);
        ui::Text("[E] back to the street", 10, GetScreenHeight() - 26, 16,
                 Fade(RAYWHITE, 0.7f));
        EndDrawing();
        return;
    }

    Camera3D cam = { 0 };
    const Vector3 look = { sinf(T.yaw) * cosf(T.pitch), sinf(T.pitch), cosf(T.yaw) * cosf(T.pitch) };
    const Vector3 eye = { T.pPos.x, T.pPos.y + 2.0f, T.pPos.z };
    cam.position = Vector3Subtract(eye, Vector3Scale(look, 6.0f));
    cam.position.y = fmaxf(cam.position.y, 0.6f);
    cam.target = Vector3Add(eye, Vector3Scale(look, 4.0f));
    cam.up = { 0, 1, 0 };
    cam.fovy = 60;
    cam.projection = CAMERA_PERSPECTIVE;

    SfxAmbience(0.10f);   // street murmur stand-in

    BeginDrawing();
    // The street sky follows the campaign clock (V64): the same hours the
    // battle sky keeps — walk a town at midnight and it is midnight.
    const float townTod = gs.dayTimer / 60.0f;
    const bool  townNight = townTod >= 0.82f || townTod < 0.06f;
    Color skyTop = { 132, 172, 220, 255 }, skyBot = { 222, 232, 240, 255 };
    if (townNight)            { skyTop = { 22, 28, 52, 255 };  skyBot = { 46, 54, 84, 255 }; }
    else if (townTod >= 0.70f){ skyTop = { 168, 106, 88, 255 }; skyBot = { 238, 176, 128, 255 }; }
    else if (townTod < 0.10f) { skyTop = { 140, 118, 116, 255 }; skyBot = { 234, 200, 166, 255 }; }
    ClearBackground(skyTop);   // clears depth too
    DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(),
                           skyTop, skyBot);

    // Faint lute from the gold-roofed door when you stand near it (N5).
    SfxMinstrel(TownAtTavern() ? 0.12f : 0.0f);

    BeginMode3D(cam);
    BeginShaderMode(GetLitShader());
    DrawPlane({ 0, 0, 0 }, { TOWN_EDGE * 2, TOWN_EDGE * 2 }, Color{ 96, 128, 72, 255 });
    DrawCylinder({ 0, 0.01f, 0 }, 16.0f, 16.0f, 0.02f, 24, Color{ 150, 134, 105, 255 }); // plaza
    DrawCylinder({ 0, 0.02f, 0 }, 1.2f, 1.4f, 0.9f, 12, Color{ 120, 110, 100, 255 });    // well

    for (const Building& b : T.buildings) {
        DrawCube({ b.pos.x, b.size.y * 0.5f, b.pos.z }, b.size.x, b.size.y, b.size.z, b.wall);
        DrawCubeWires({ b.pos.x, b.size.y * 0.5f, b.pos.z }, b.size.x, b.size.y, b.size.z,
                      Fade(BLACK, 0.35f));
        if (townNight)   // a lit window after dark (V64)
            DrawCube({ b.pos.x, b.size.y * 0.55f, b.pos.z + b.size.z * 0.5f + 0.02f },
                     0.9f, 0.9f, 0.03f, Color{ 255, 214, 130, 255 });
        if (b.flatTop) {   // crenellated top instead of a roof
            for (float cx = -b.size.x / 2; cx < b.size.x / 2; cx += 2.4f)
                DrawCube({ b.pos.x + cx + 0.6f, b.size.y + 0.4f, b.pos.z },
                         1.1f, 0.8f, fminf(b.size.z, 1.6f), b.roof);
        } else {
            // pitched roof: a squashed 4-sided cone
            DrawCylinderEx({ b.pos.x, b.size.y, b.pos.z },
                           { b.pos.x, b.size.y + 2.4f, b.pos.z },
                           fmaxf(b.size.x, b.size.z) * 0.72f, 0.0f, 4, b.roof);
        }
        if (b.tavern)   // hanging sign: a keg (or the lord's banner on a keep)
            DrawSphere({ b.pos.x, b.size.y + 3.4f, b.pos.z }, 0.7f, GOLD);
    }

    // Market stalls ring the plaza (N5): posts, a tinted canopy, and a
    // crate of wares — colours pulled from the goods catalogue so the
    // stalls read as the market they front.
    if (T.stalls) {
        for (int s = 0; s < 3; ++s) {
            const float ang = 0.8f + s * 2.0f;
            const float sx = sinf(ang) * 12.5f, sz = cosf(ang) * 12.5f;
            const Color canopy = c.goods.size() > 0
                ? c.goods[(s * 2) % c.goods.size()].tint : BEIGE;
            for (const float px : { -1.2f, 1.2f })
                for (const float pz : { -0.9f, 0.9f })
                    DrawCube({ sx + px, 1.0f, sz + pz }, 0.18f, 2.0f, 0.18f,
                             Color{ 110, 82, 52, 255 });
            DrawCube({ sx, 2.05f, sz }, 3.0f, 0.14f, 2.2f, canopy);
            DrawCube({ sx, 0.5f, sz }, 1.6f, 1.0f, 1.0f,
                     Color{ 132, 100, 62, 255 });   // the wares crate
        }
    }

    for (const Npc& n : T.npcs) {
        DrawCylinder({ n.pos.x, 0.03f, n.pos.z }, 0.45f, 0.45f, 0.02f, 12,
                     Fade(BLACK, 0.25f));
        Pose pose;
        pose.yaw = n.yaw;
        pose.walkPhase = n.walkPhase;
        DrawCharacter(c, n.pos, n.loadout, pose, BEIGE);
    }

    DrawCylinder({ T.pPos.x, 0.03f, T.pPos.z }, 0.5f, 0.5f, 0.02f, 12,
                 Fade(BLACK, 0.28f));
    Pose hero;
    hero.yaw = T.yaw;
    hero.walkPhase = T.walkPhase;
    DrawCharacter(c, T.pPos, gs.playerHero.loadout, hero, Color{ 40, 120, 255, 255 });
    EndShaderMode();
    EndMode3D();

    // ---- speech: whoever stands close has a word for you ----
    for (const Npc& n : T.npcs) {
        const float dx = n.pos.x - T.pPos.x, dz = n.pos.z - T.pPos.z;
        if (dx * dx + dz * dz > 4.5f * 4.5f || n.line.empty()) continue;
        const Vector2 sp = GetWorldToScreen({ n.pos.x, n.pos.y + 2.5f, n.pos.z }, cam);
        if (sp.x < 0 || sp.x > GetScreenWidth() || sp.y < 0) continue;
        const int tw = ui::Measure(n.line.c_str(), 17);
        DrawRectangle((int)sp.x - tw / 2 - 8, (int)sp.y - 12, tw + 16, 26, Fade(BLACK, 0.65f));
        ui::Text(n.line.c_str(), (int)sp.x - tw / 2, (int)sp.y - 8, 17, RAYWHITE);
    }

    // ---- HUD ----
    DrawRectangle(0, 0, GetScreenWidth(), 34, Fade(BLACK, 0.6f));
    ui::Text(TextFormat("%s  ·  Gold: %d   Party: %d", town.name.c_str(), gs.gold,
                        gs.player.totalTroops()), 10, 8, 20, RAYWHITE);

    g_svcHits.clear();
    const Vector2 svcMouse = GetMousePosition();
    // A clickable chip (V122): prints the label, records its rect, and
    // brightens under the mouse — the key still works, the mouse now does too.
    auto Chip = [&](const char* label, int x, int y, int size, Color col,
                    int id) -> int {
        const int w = ui::Measure(label, size);
        const bool hov = svcMouse.x >= x - 4 && svcMouse.x < x + w + 4 &&
                         svcMouse.y >= y - 3 && svcMouse.y < y + size + 5;
        if (hov) DrawRectangle(x - 4, y - 3, w + 8, size + 8, Fade(GOLD, 0.22f));
        ui::Text(label, x, y, size, hov ? RAYWHITE : col);
        g_svcHits.push_back({ x - 4, y - 3, w + 8, size + 8, id });
        return w;
    };
    if (TownAtTavern()) {
        const std::vector<int>& roster = c.factions[c.playerFaction].roster;
        int captives = 0;
        for (int n : gs.prisoners) captives += n;
        const int y = GetScreenHeight() - 60 - (int)roster.size() * 24 - (captives > 0 ? 24 : 0);
        DrawRectangle(0, y - 8, GetScreenWidth(), GetScreenHeight() - y + 8, Fade(BLACK, 0.7f));
        ui::Text(TextFormat("The tavern. Recruits wait for coin (%d in the pool):",
                            gs.towns[gs.currentSettlement].recruitPool),
                 10, y, 20, GOLD);
        if (captives > 0)
            Chip(TextFormat("[R] Ransom %d captives (%d gold)", captives, captives * 10),
                 10, GetScreenHeight() - 54, 20, LIME, SVC_RANSOM);
        for (int slot = 0; slot < (int)roster.size(); ++slot) {
            const TroopDef& td = c.troops[roster[slot]];
            Chip(TextFormat("[%d] %s - %d gold  (have %d)", slot + 1, td.name.c_str(),
                            td.cost, gs.player.troopCounts[roster[slot]]),
                 10, y + 26 + slot * 24, 20, RAYWHITE, SVC_RECRUIT0 + slot);
        }
    } else {
        // The local services, always on show (K7) — and clickable (V122).
        struct { const char* label; int id; } svcs[] = {
            { "[T] tournament", SVC_TOURNEY }, { "[M] market",   SVC_MARKET },
            { "[G] work",       SVC_WORK },    { "[H] hire",     SVC_HIRE },
            { "[V] oath",       SVC_OATH },    { "[E] talk",     SVC_TALK },
            { "[F] garrison (yours)", SVC_GARRISON },
        };
        int sx = 10;
        for (const auto& s : svcs)
            sx += Chip(s.label, sx, GetScreenHeight() - 50, 18,
                       Fade(GOLD, 0.85f), s.id) + 26;
        ui::Text("WASD walk, mouse look. The gold roof is the tavern. Esc: gate menu.",
                 10, GetScreenHeight() - 26, 16, Fade(RAYWHITE, 0.7f));
    }

    // The gate menu (U4): the town breathes behind a clear list of
    // everything there is to do here — boots are one row among many.
    if (T.menu && gs.currentSettlement >= 0) {
        const Town& town = gs.towns[gs.currentSettlement];
        const Content& c = gs.content;
        const int cx = GetScreenWidth() / 2;
        const int x0 = cx - townmenu::X_HALF;
        DrawRectangle(x0 - 30, 40, townmenu::X_HALF * 2 + 60,
                      townmenu::Y + townmenu::ROWS * townmenu::ROW_H - 10,
                      Fade(BLACK, 0.92f));
        DrawRectangleLines(x0 - 30, 40, townmenu::X_HALF * 2 + 60,
                           townmenu::Y + townmenu::ROWS * townmenu::ROW_H - 10,
                           GOLD);
        ui::Title(town.name.c_str(), x0, 56, 40, GOLD);
        const bool ownerValid = town.owner >= 0 && town.owner < c.factions.size();
        ui::Text(TextFormat("%s of %s      prosperity %d%%      garrison %d%s%s",
                            town.type == SettlementType::Village ? "Village"
                            : town.type == SettlementType::Castle ? "Castle"
                                                                  : "Town",
                            ownerValid ? c.factions[town.owner].name.c_str()
                                       : "no crown",
                            town.prosperity, town.garrisonSize(),
                            town.fortified ? "      FORTIFIED" : "",
                            town.warMarkup > 100
                                ? TextFormat("      war prices +%d%%",
                                             town.warMarkup - 100)
                                : ""),
                 x0, 110, 19,
                 town.warMarkup > 100 ? Fade(Color{ 255, 180, 120, 255 }, 0.95f)
                                      : RAYWHITE);
        const int vault = gs.currentSettlement < (int)gs.bankAt.size()
                              ? gs.bankAt[gs.currentSettlement] : 0;
        ui::Text(TextFormat("Your purse: %d gold      your band: %d / %d"
                            "%s",
                            gs.gold, gs.player.totalTroops(), PartyCap(gs),
                            vault > 0
                                ? TextFormat("      in the vault here: %d", vault)
                                : ""),
                 x0, 136, 17, Fade(RAYWHITE, 0.7f));
        char tavernRow[96];
        snprintf(tavernRow, sizeof(tavernRow),
                 "[2]  The tavern            (%d recruits in the pool)",
                 town.recruitPool);
        // The tavern names its guest (V79): who is drinking here, what
        // they're good for, and whether they've already taken your coin.
        char hireRow[112];
        bool hireLive = true;
        {
            int comp = -1, nComps = 0;
            for (int t = 0; t < c.troops.size(); ++t)
                if (c.troops[t].companion) nComps++;
            if (nComps > 0) {
                int idx = gs.currentSettlement % nComps, seen = 0;
                for (int t = 0; t < c.troops.size(); ++t)
                    if (c.troops[t].companion && seen++ == idx) { comp = t; break; }
            }
            if (comp >= 0)
                snprintf(hireRow, sizeof(hireRow),
                         "[5]  Hire %-14s (%s, %d gold)%s",
                         c.troops[comp].name.c_str(),
                         c.troops[comp].perk.empty() ? "blade"
                                                     : c.troops[comp].perk.c_str(),
                         c.troops[comp].cost,
                         gs.player.troopCounts[comp] > 0 ? "  - already yours" : "");
            if (comp >= 0) hireLive = gs.player.troopCounts[comp] == 0;
            if (comp < 0)
                snprintf(hireRow, sizeof(hireRow), "[5]  Hire the companion");
        }
        const char* rows[townmenu::ROWS] = {
            "[1]  The market            (buy, sell, arms, the moneylender)",
            tavernRow,
            "[3]  The tournament        (Shift-click to stake 50)",
            "[4]  Seek work             (the local quest)",
            hireRow,
            "[6]  Swear to this crown",
            "[7]  The hall              (court, news, politics)",
            "[8]  Garrison a soldier    (yours only)",
            "[9]  Recall a soldier",
            "[W]  Visit the settlement  (walk the streets)",
            "[0]  Host a feast          (200 gold; lords and matches)",
            "[J]  Sellswords for hire   (a 5-man pack, 150 gold)",
            "[F]  Fortify the walls     (500 gold; +10 garrison, harder to storm)",
        };
        // Rows that can't act right now grey out with the reason implicit
        // (V4): buttons that look alive ARE alive.
        const bool mine   = town.owner == c.playerFaction;
        const bool sworn  = gs.liege >= 0;
        bool live[townmenu::ROWS];
        for (int r = 0; r < townmenu::ROWS; ++r) live[r] = true;
        live[4] = hireLive;                 // hire: once each, ever (V79)
        live[5] = !sworn && !mine;          // swear: free captains only
        live[7] = mine;                     // garrison a soldier
        live[8] = mine && town.garrisonSize() > 0;   // recall
        live[10] = mine && gs.feastDays <= 0;        // host a feast (V34)
        live[11] = town.type == SettlementType::Town;   // sellswords (V47)
        live[12] = mine && !town.fortified;             // fortify (V51)
        // Dead rows say why (V10): a greyed button that explains itself.
        const char* why[townmenu::ROWS] = { nullptr };
        if (!live[4]) why[4] = "(already yours)";
        if (!live[5]) why[5] = sworn ? "(already sworn)" : "(your own seat)";
        if (!live[7]) why[7] = "(not your walls)";
        if (!live[8]) why[8] = mine ? "(the wall is bare)" : "(not your walls)";
        if (!live[10]) why[10] = mine ? "(a feast already holds)" : "(not your hall)";
        if (!live[11]) why[11] = "(they drink in towns)";
        if (!live[12]) why[12] = mine ? "(already fortified)" : "(not your walls)";
        int y = townmenu::Y;
        const Vector2 m = GetMousePosition();
        for (int r = 0; r < townmenu::ROWS; ++r) {
            if (live[r] && m.x >= x0 && m.x < x0 + townmenu::X_HALF * 2 &&
                m.y >= y && m.y < y + townmenu::ROW_H)
                DrawRectangle(x0 - 8, y, townmenu::X_HALF * 2 + 16,
                              townmenu::ROW_H, Fade(GOLD, 0.14f));
            ui::Text(rows[r], x0, y + 6, 20,
                     live[r] ? RAYWHITE : Fade(RAYWHITE, 0.35f));
            if (!live[r] && why[r])
                ui::Text(why[r],
                         x0 + townmenu::X_HALF * 2 - 10 -
                             ui::Measure(why[r], 15),
                         y + 9, 15, Fade(GOLD, 0.5f));
            y += townmenu::ROW_H;
        }
        ui::Text("[Esc] ride on", x0, y + 4, 17, Fade(RAYWHITE, 0.6f));
        // Click feedback (V2): the last thing that happened, right here.
        if (!gs.resultText.empty())
            ui::Text(gs.resultText.c_str(), x0, y + 30, 17, GOLD);
    }
    EndDrawing();
}
