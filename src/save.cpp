#include "save.h"
#include "campaign/campaign.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace {

constexpr int SAVE_VERSION = 1;

// Write one troop-count vector as "<tag> <troopId> <count>" lines.
void WriteTroops(std::ofstream& f, const Content& c, const char* tag,
                 const std::vector<int>& counts) {
    for (int t = 0; t < (int)counts.size() && t < c.troops.size(); ++t)
        if (counts[t] > 0)
            f << tag << ' ' << c.troops[t].id << ' ' << counts[t] << '\n';
}

const char* SlotName(EquipSlot s) {
    switch (s) {
        case EquipSlot::Head:   return "head";
        case EquipSlot::Body:   return "body";
        case EquipSlot::Hands:  return "hands";
        case EquipSlot::Feet:   return "feet";
        case EquipSlot::Weapon: return "weapon";
        case EquipSlot::Count:  break;
    }
    return "?";
}

int SlotFromName(const std::string& n) {
    for (int s = 0; s < EQUIP_SLOT_COUNT; ++s)
        if (n == SlotName(static_cast<EquipSlot>(s))) return s;
    return -1;
}

}  // namespace

namespace {
const char* SavePathNamed(char* buf, size_t n, const char* name) {
    if (IsWindowReady()) std::snprintf(buf, n, "%s%s", GetApplicationDirectory(), name);
    else                 std::snprintf(buf, n, "%s", name);
    return buf;
}
}  // namespace

const char* DefaultSavePath() {
    static char path[512];
    return SavePathNamed(path, sizeof(path), "save.owb");
}

const char* AutoSavePath() {
    static char path[512];
    return SavePathNamed(path, sizeof(path), "autosave.owb");
}

bool SaveGame(const GameState& gs, const char* path) {
    const Content& c = gs.content;
    std::ofstream f(path);
    if (!f) return false;

    f << "OWSAVE " << SAVE_VERSION << '\n';
    f << "gold " << gs.gold << '\n';
    f << "day " << gs.day << '\n';
    f << "playerpos " << gs.player.pos.x << ' ' << gs.player.pos.y << '\n';
    WriteTroops(f, c, "ptroop", gs.player.troopCounts);
    WriteTroops(f, c, "pxp",    gs.troopXp);   // veterancy pools
    WriteTroops(f, c, "captive", gs.prisoners);

    // Hero progression.
    f << "hero " << gs.playerHero.level << ' ' << gs.playerHero.xp << ' '
      << gs.playerHero.attrPoints << '\n';
    for (int a = 0; a < c.attributes.size() && a < (int)gs.playerHero.attributes.size(); ++a)
        if (gs.playerHero.attributes[a] > 0)
            f << "attr " << c.attributes[a].id << ' ' << gs.playerHero.attributes[a] << '\n';

    // Hero equipment: worn slots by id, then the carried arsenal in order.
    for (int s = 0; s < EQUIP_SLOT_COUNT; ++s) {
        if (s == (int)EquipSlot::Weapon) continue;
        const int h = gs.playerHero.loadout.slots[s];
        if (c.armor.valid(h))
            f << "wear " << SlotName((EquipSlot)s) << ' ' << c.armor[h].id << '\n';
    }
    for (int w : gs.playerHero.loadout.weapons)
        if (c.weapons.valid(w)) f << "carry " << c.weapons[w].id << '\n';

    // Fitted companion gear (K6): worn slots and carried arsenal per hero.
    for (const auto& p : gs.companionGear) {
        if (p.first < 0 || p.first >= c.troops.size()) continue;
        const std::string& tid = c.troops[p.first].id;
        for (int s = 0; s < EQUIP_SLOT_COUNT; ++s) {
            if (s == (int)EquipSlot::Weapon) continue;
            const int h = p.second.slots[s];
            if (c.armor.valid(h))
                f << "cwear " << tid << ' ' << SlotName((EquipSlot)s) << ' '
                  << c.armor[h].id << '\n';
        }
        for (int w : p.second.weapons)
            if (c.weapons.valid(w)) f << "ccarry " << tid << ' ' << c.weapons[w].id << '\n';
    }

    // Tiled inventory contents.
    for (const InvItem& it : gs.inventory) {
        const bool ok = it.isWeapon ? c.weapons.valid(it.handle) : c.armor.valid(it.handle);
        if (!ok) continue;
        f << "item " << (it.isWeapon ? "weapon" : "armor") << ' '
          << (it.isWeapon ? c.weapons[it.handle].id : c.armor[it.handle].id) << ' '
          << it.x << ' ' << it.y << '\n';
    }

    // The active quest (F4).
    if (gs.activeQuest >= 0 && gs.activeQuest < c.quests.size())
        f << "quest " << c.quests[gs.activeQuest].id << ' '
          << (gs.questFaction >= 0 && gs.questFaction < c.factions.size()
                  ? c.factions[gs.questFaction].id : std::string("none"))
          << ' ' << gs.questTown << ' ' << gs.questProgress << '\n';

    // Burned-out bandit dens (H2) — live ones are recreated from the map.
    for (int l = 0; l < (int)gs.lairs.size(); ++l)
        if (!gs.lairs[l].alive) f << "lairdead " << l << '\n';

    // The claimed crown (F3).
    if (gs.crowned) f << "crowned 1\n";

    // Standing duties (K5).
    if (gs.musterTown >= 0)
        f << "muster " << gs.musterTown << ' ' << gs.musterDays << '\n';
    if (gs.lordsRally)
        f << "rally " << gs.lordsRallyPos.x << ' ' << gs.lordsRallyPos.y << ' '
          << gs.lordsRallyDays << '\n';

    // The sworn liege (F2).
    if (gs.liege >= 0 && gs.liege < c.factions.size())
        f << "liege " << c.factions[gs.liege].id << '\n';

    // Standing with each faction (F1).
    for (int fa = 0; fa < c.factions.size() && fa < (int)gs.relations.size(); ++fa)
        if (gs.relations[fa] != 0)
            f << "relation " << c.factions[fa].id << ' ' << gs.relations[fa] << '\n';

    // Trade goods in the saddlebags (E1).
    for (int g = 0; g < c.goods.size() && g < (int)gs.goods.size(); ++g)
        if (gs.goods[g] > 0)
            f << "good " << c.goods[g].id << ' ' << gs.goods[g] << '\n';

    // Settlement ownership (towns themselves are recreated by CampaignInit;
    // only who holds them is state). Keyed by index — the town list is static.
    for (int t = 0; t < (int)gs.towns.size(); ++t) {
        if (gs.towns[t].owner >= 0 && gs.towns[t].owner < c.factions.size())
            f << "town " << t << ' ' << c.factions[gs.towns[t].owner].id << '\n';
        // Explicit reset so an emptied garrison doesn't resurrect on load.
        f << "garrisonreset " << t << '\n';
        for (int tr = 0; tr < (int)gs.towns[t].garrison.size() && tr < c.troops.size(); ++tr)
            if (gs.towns[t].garrison[tr] > 0)
                f << "garrison " << t << ' ' << c.troops[tr].id << ' '
                  << gs.towns[t].garrison[tr] << '\n';
        for (int g = 0; g < (int)gs.towns[t].stock.size() && g < c.goods.size(); ++g)
            f << "stock " << t << ' ' << c.goods[g].id << ' '
              << gs.towns[t].stock[g] << ' ' << gs.towns[t].priceOffset[g] << '\n';
        if (gs.towns[t].prosperity != 100)
            f << "prosper " << t << ' ' << gs.towns[t].prosperity << '\n';
        if (t < (int)gs.enterpriseAt.size() && c.enterprises.valid(gs.enterpriseAt[t]))
            f << "enterprise " << t << ' ' << c.enterprises[gs.enterpriseAt[t]].id << '\n';
    }

    // Live diplomacy: only pairs that differ from the base relations (or hold
    // an active truce/war score) need recording; the rest re-derive on load.
    {
        const int nf = c.factions.size();
        if ((int)gs.hostile.size() == nf * nf)
            for (int a = 0; a < nf; ++a)
                for (int b = a + 1; b < nf; ++b) {
                    const size_t ij = (size_t)a * nf + b;
                    const bool now  = gs.hostile[ij] != 0;
                    if (now == AreFactionsHostile(c, a, b) &&
                        gs.warScore[ij] == 0 && gs.truceDays[ij] <= 0) continue;
                    f << "diplo " << c.factions[a].id << ' ' << c.factions[b].id << ' '
                      << (now ? 1 : 0) << ' ' << gs.warScore[ij] << ' '
                      << gs.truceDays[ij] << '\n';
                }
    }

    for (const Party& p : gs.parties) {
        if (!p.alive) continue;
        if (p.faction < 0 || p.faction >= c.factions.size()) continue;
        if (p.caravan)
            f << "cparty " << c.factions[p.faction].id << ' '
              << p.pos.x << ' ' << p.pos.y << ' ' << p.caravanTo << '\n';
        else {
            f << "party " << c.factions[p.faction].id << ' '
              << p.pos.x << ' ' << p.pos.y;
            if (!p.lord.empty()) f << ' ' << p.lord;   // one-token lord names
            f << '\n';
        }
        WriteTroops(f, c, "troop", p.troopCounts);
    }
    return f.good();
}

bool LoadGame(GameState& gs, const char* path) {
    std::ifstream f(path);
    if (!f) return false;

    std::string header;
    int version = 0;
    f >> header >> version;
    if (header != "OWSAVE" || version != SAVE_VERSION) return false;

    // Start from a fresh world (towns etc.), then overwrite with saved state.
    {
        Content saved = std::move(gs.content);
        gs = GameState{};
        gs.content = std::move(saved);
    }
    CampaignInit(gs);
    gs.parties.clear();
    gs.player.troopCounts.assign(gs.content.troops.size(), 0);
    gs.playerHero.loadout = Loadout{};

    const Content& c = gs.content;
    std::string line;
    std::getline(f, line);   // finish the header line
    Party* cur = nullptr;    // party whose "troop" lines we're reading
    int lastCcarryTroop = -1;   // first ccarry per companion clears the arsenal
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string tag;
        if (!(ss >> tag)) continue;
        if (tag == "gold") ss >> gs.gold;
        else if (tag == "day") ss >> gs.day;
        else if (tag == "playerpos") ss >> gs.player.pos.x >> gs.player.pos.y;
        else if (tag == "ptroop" || tag == "troop" || tag == "pxp" || tag == "captive") {
            std::string id; int n = 0;
            ss >> id >> n;
            const int t = c.troops.find(id.c_str());
            if (t < 0 || n <= 0) continue;   // troop type no longer exists
            if (tag == "ptroop")      gs.player.troopCounts[t] += n;
            else if (tag == "pxp")    gs.troopXp[t] += n;
            else if (tag == "captive") gs.prisoners[t] += n;
            else if (cur)             cur->troopCounts[t] += n;
        } else if (tag == "wear") {
            std::string slot, id;
            ss >> slot >> id;
            const int s = SlotFromName(slot);
            const int h = c.armor.find(id.c_str());
            if (s >= 0 && h >= 0) gs.playerHero.loadout.slots[s] = h;
        } else if (tag == "carry") {
            std::string id;
            ss >> id;
            const int h = c.weapons.find(id.c_str());
            if (h >= 0) gs.playerHero.loadout.addWeapon(h);
        } else if (tag == "town") {
            int idx = -1; std::string fid;
            ss >> idx >> fid;
            const int fh = c.factions.find(fid.c_str());
            if (idx >= 0 && idx < (int)gs.towns.size() && fh >= 0)
                gs.towns[idx].owner = fh;
        } else if (tag == "hero") {
            ss >> gs.playerHero.level >> gs.playerHero.xp >> gs.playerHero.attrPoints;
        } else if (tag == "attr") {
            std::string id; int v = 0;
            ss >> id >> v;
            const int a = c.attributes.find(id.c_str());
            if (a >= 0 && a < (int)gs.playerHero.attributes.size())
                gs.playerHero.attributes[a] = v;
        } else if (tag == "item") {
            std::string kind, id;
            InvItem it;
            ss >> kind >> id >> it.x >> it.y;
            it.isWeapon = (kind == "weapon");
            it.handle = it.isWeapon ? c.weapons.find(id.c_str()) : c.armor.find(id.c_str());
            if (it.handle >= 0) gs.inventory.push_back(it);
        } else if (tag == "good") {
            std::string id; int n = 0;
            ss >> id >> n;
            const int g = c.goods.find(id.c_str());
            if (g >= 0 && g < (int)gs.goods.size() && n > 0) gs.goods[g] = n;
        } else if (tag == "stock") {
            int idx = -1, n = 0, off = 100; std::string gid;
            ss >> idx >> gid >> n >> off;
            const int g = c.goods.find(gid.c_str());
            if (idx >= 0 && idx < (int)gs.towns.size() && g >= 0 &&
                g < (int)gs.towns[idx].stock.size()) {
                gs.towns[idx].stock[g] = n;
                gs.towns[idx].priceOffset[g] = off;
            }
        } else if (tag == "garrisonreset") {
            int idx = -1;
            ss >> idx;
            if (idx >= 0 && idx < (int)gs.towns.size())
                gs.towns[idx].garrison.assign(c.troops.size(), 0);
        } else if (tag == "garrison") {
            int idx = -1, n = 0; std::string tid;
            ss >> idx >> tid >> n;
            const int tr = c.troops.find(tid.c_str());
            if (idx >= 0 && idx < (int)gs.towns.size() && tr >= 0 && n > 0)
                gs.towns[idx].garrison[tr] += n;
        } else if (tag == "diplo") {
            std::string ida, idb; int war = 0; int score = 0; float truce = 0;
            ss >> ida >> idb >> war >> score >> truce;
            const int a = c.factions.find(ida.c_str());
            const int b = c.factions.find(idb.c_str());
            const int nf = c.factions.size();
            if (a >= 0 && b >= 0 && a != b && (int)gs.hostile.size() == nf * nf) {
                const size_t ij = (size_t)a * nf + b, ji = (size_t)b * nf + a;
                gs.hostile[ij] = gs.hostile[ji] = (unsigned char)(war ? 1 : 0);
                gs.warScore[ij] = gs.warScore[ji] = score;
                gs.truceDays[ij] = gs.truceDays[ji] = truce;
            }
        } else if (tag == "lairdead") {
            int idx = -1;
            ss >> idx;
            if (idx >= 0 && idx < (int)gs.lairs.size()) gs.lairs[idx].alive = false;
        } else if (tag == "crowned") {
            int v = 0;
            ss >> v;
            gs.crowned = v != 0;
        } else if (tag == "quest") {
            std::string qid, fid;
            ss >> qid >> fid >> gs.questTown >> gs.questProgress;
            gs.activeQuest  = c.quests.find(qid.c_str());
            gs.questFaction = c.factions.find(fid.c_str());
        } else if (tag == "cwear") {
            std::string tid, slot, id;
            ss >> tid >> slot >> id;
            const int t = c.troops.find(tid.c_str());
            const int s = SlotFromName(slot);
            const int h = c.armor.find(id.c_str());
            if (t >= 0 && s >= 0 && h >= 0) {
                Loadout& lo = CompanionGear(gs, t);
                lo.slots[s] = h;
            }
        } else if (tag == "ccarry") {
            std::string tid, id;
            ss >> tid >> id;
            const int t = c.troops.find(tid.c_str());
            const int h = c.weapons.find(id.c_str());
            if (t >= 0 && h >= 0) {
                Loadout& lo = CompanionGear(gs, t);
                // First ccarry line replaces the catalogue arsenal wholesale.
                if (lastCcarryTroop != t) {
                    lo.weapons.clear();
                    lo.slots[(int)EquipSlot::Weapon] = -1;
                    lastCcarryTroop = t;
                }
                lo.addWeapon(h);
            }
        } else if (tag == "muster") {
            ss >> gs.musterTown >> gs.musterDays;
        } else if (tag == "rally") {
            gs.lordsRally = true;
            ss >> gs.lordsRallyPos.x >> gs.lordsRallyPos.y >> gs.lordsRallyDays;
        } else if (tag == "liege") {
            std::string fid;
            ss >> fid;
            gs.liege = c.factions.find(fid.c_str());
        } else if (tag == "relation") {
            std::string fid; int v = 0;
            ss >> fid >> v;
            const int fh = c.factions.find(fid.c_str());
            if (fh >= 0 && fh < (int)gs.relations.size()) gs.relations[fh] = v;
        } else if (tag == "enterprise") {
            int idx = -1; std::string eid;
            ss >> idx >> eid;
            const int e = c.enterprises.find(eid.c_str());
            if (idx >= 0 && idx < (int)gs.enterpriseAt.size() && e >= 0)
                gs.enterpriseAt[idx] = e;
        } else if (tag == "prosper") {
            int idx = -1, v = 100;
            ss >> idx >> v;
            if (idx >= 0 && idx < (int)gs.towns.size()) gs.towns[idx].prosperity = v;
        } else if (tag == "party" || tag == "cparty") {
            std::string fid; Vector2 pos{};
            ss >> fid >> pos.x >> pos.y;
            const int fh = c.factions.find(fid.c_str());
            if (fh < 0) { cur = nullptr; continue; }
            Party p;
            p.faction = fh;
            p.pos = p.wanderTarget = pos;
            p.troopCounts.assign(c.troops.size(), 0);
            if (tag == "cparty") { p.caravan = true; ss >> p.caravanTo; }
            else                 ss >> p.lord;   // optional trailing lord name
            gs.parties.push_back(p);
            cur = &gs.parties.back();
        }
    }
    gs.screen = Screen::Campaign;
    return true;
}
