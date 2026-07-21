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
    const int f = gs.towns[gs.currentSettlement].owner;
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
    SfxPlay(Sfx::Fanfare);
    return msg;
}

// Take the local giver's quest (F4). Returns the offer, or why there is none —
// shared by the G hotkey and the lord's court dialogue (K2).
std::string TryQuest(GameState& gs) {
    const Content& c = gs.content;
    if (gs.activeQuest >= 0) return "Finish the task you carry first.";
    if (c.quests.size() == 0) return "No work today.";
    const int q = (gs.currentSettlement + gs.day) % c.quests.size();
    const QuestDef& qd = c.quests[q];
    gs.activeQuest  = q;
    gs.questFaction = gs.towns[gs.currentSettlement].owner;
    gs.questTown    = -1;
    gs.questProgress = 0;
    if (qd.type == QuestType::DeliverGrain) {
        // Deliver to the nearest settlement you can actually walk into.
        float bestD = 1e9f;
        for (int t = 0; t < (int)gs.towns.size(); ++t) {
            if (t == gs.currentSettlement) continue;
            if (AtWar(gs, gs.towns[t].owner, c.playerFaction)) continue;
            const float d = Vector2Distance(gs.towns[gs.currentSettlement].pos,
                                            gs.towns[t].pos);
            if (d < bestD) { bestD = d; gs.questTown = t; }
        }
        if (gs.questTown < 0) {
            gs.activeQuest = -1;   // nowhere to deliver: no quest today
            return "No work today.";
        }
    }
    const std::string msg = gs.questTown >= 0
        ? TextFormat("%s: %s  Bring %d grain to %s.", qd.name.c_str(),
                     qd.blurb.c_str(), qd.amount, gs.towns[gs.questTown].name.c_str())
        : TextFormat("%s: %s  (%d gold)", qd.name.c_str(), qd.blurb.c_str(),
                     qd.goldReward);
    gs.resultText = msg;
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
    if (IsWindowReady()) DisableCursor();
    T.hasLastMouse = false;
}

bool TownUpdate(GameState& gs, float dt, const BattleInput& in, const CampaignInput& cin) {
    if (dt > 0.05f) dt = 0.05f;
    const Content& c = gs.content;

    // ---- leave ----
    if (cin.leaveSettlement) {
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

    // ---- tavern recruiting ----
    if (TownAtTavern() && cin.recruitSlot >= 0) {
        const std::vector<int>& roster = c.factions[c.playerFaction].roster;
        if (cin.recruitSlot < (int)roster.size()) {
            const TroopDef& td = c.troops[roster[cin.recruitSlot]];
            if (gs.player.totalTroops() >= PartyCap(gs)) {
                gs.resultText = TextFormat(
                    "Your name only carries %d men. Win renown for more.",
                    PartyCap(gs));
            } else if (gs.gold >= td.cost) {
                gs.gold -= td.cost;
                gs.player.troopCounts[roster[cin.recruitSlot]]++;
                SfxPlay(Sfx::Click);
            }
        }
    }

    // ---- ransom captives (flat gold a head; TODO(balance)) ----
    if (TownAtTavern() && cin.ransom) {
        int heads = 0;
        for (int& n : gs.prisoners) { heads += n; n = 0; }
        if (heads > 0) {
            const int gold = heads * 10;
            gs.gold += gold;
            gs.resultText = TextFormat("Ransomed %d captives for %d gold.", heads, gold);
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
    if (owner == c.playerFaction)
        gs.dialogueName = "Your Castellan";
    else if (c.factions.valid(owner) && !c.factions[owner].lords.empty())
        gs.dialogueName = TextFormat("Lord %s", c.factions[owner].lords[0].c_str());
    else
        gs.dialogueName = "The Castellan";
    gs.dialogueLord = true;
    gs.dialogueLines.clear();
    gs.dialogueLines.push_back("Speak, captain. The court listens.");
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
    gs.dialogueLines.clear();
    gs.dialogueLines.push_back(best ? best->line : "Well met, captain.");
}

// Grant the seat you stand in to a raised lord without one (M3). Returns the
// court's answer — shared by the dialogue topic and any future hotkey.
std::string TryGrantFief(GameState& gs) {
    const Content& c = gs.content;
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
        gs.screen = Screen::Settlement;
        gs.dialogueLines.clear();
        if (IsWindowReady()) DisableCursor();
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

    int y = 270;
    for (const std::string& line : gs.dialogueLines) {
        ui::Text(TextFormat("\"%s\"", line.c_str()), x, y, 22, RAYWHITE);
        y += 32;
    }

    ui::Text("[1] What news of the war?", x, y + 30, 22, Fade(RAYWHITE, 0.85f));
    if (gs.dialogueLord) {
        ui::Text("[2] I would swear my sword to this crown.", x, y + 60, 22,
                 Fade(RAYWHITE, 0.85f));
        ui::Text("[3] Have you work for my warband?", x, y + 90, 22,
                 Fade(RAYWHITE, 0.85f));
        int leaveY = y + 120;
        if (gs.crowned) {   // a ruler's court has a ruler's business (M3)
            ui::Text("[4] I grant this seat to a lord of mine.", x, y + 120, 22,
                     Fade(GOLD, 0.85f));
            leaveY += 30;
        }
        if (gs.feastTown == gs.currentSettlement && gs.feastDays > 0 &&
            gs.spouseFaction < 0) {   // a feast's court makes matches (M5)
            ui::Text("[5] I seek a marriage alliance.", x, leaveY, 22,
                     Fade(PINK, 0.85f));
            leaveY += 30;
        }
        ui::Text("[Esc / E] Take your leave", x, leaveY, 20, Fade(RAYWHITE, 0.6f));
    } else {
        ui::Text("[2] Any work for a warband?", x, y + 60, 22, Fade(RAYWHITE, 0.85f));
        ui::Text("[Esc / E] Take your leave", x, y + 90, 20, Fade(RAYWHITE, 0.6f));
    }
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
    ClearBackground(Color{ 132, 172, 220, 255 });   // clears depth too
    DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(),
                           Color{ 132, 172, 220, 255 }, Color{ 222, 232, 240, 255 });

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

    if (TownAtTavern()) {
        const std::vector<int>& roster = c.factions[c.playerFaction].roster;
        int captives = 0;
        for (int n : gs.prisoners) captives += n;
        const int y = GetScreenHeight() - 60 - (int)roster.size() * 24 - (captives > 0 ? 24 : 0);
        DrawRectangle(0, y - 8, GetScreenWidth(), GetScreenHeight() - y + 8, Fade(BLACK, 0.7f));
        ui::Text("The tavern. Recruits wait for coin:", 10, y, 20, GOLD);
        if (captives > 0)
            ui::Text(TextFormat("[R] Ransom %d captives (%d gold)", captives, captives * 10),
                     10, GetScreenHeight() - 54, 20, LIME);
        for (int slot = 0; slot < (int)roster.size(); ++slot) {
            const TroopDef& td = c.troops[roster[slot]];
            ui::Text(TextFormat("[%d] %s - %d gold  (have %d)", slot + 1, td.name.c_str(),
                                td.cost, gs.player.troopCounts[roster[slot]]),
                     10, y + 26 + slot * 24, 20, RAYWHITE);
        }
    } else {
        // The local keys, always on show (K7) — every settlement service in
        // one line, so nothing shipped stays undiscovered.
        ui::Text("[T] tournament   [M] market   [G] work   [H] hire   [V] oath   [E] talk",
                 10, GetScreenHeight() - 50, 18, Fade(GOLD, 0.85f));
        ui::Text("WASD walk, mouse look. The gold roof is the tavern. Esc leaves.",
                 10, GetScreenHeight() - 26, 16, Fade(RAYWHITE, 0.7f));
    }
    EndDrawing();
}
