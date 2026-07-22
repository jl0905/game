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

const char* SaveSlotPath(int slot) {
    static char path[512];
    char name[32];
    std::snprintf(name, sizeof(name), "slot_%d.owb", slot);
    return SavePathNamed(path, sizeof(path), name);
}

bool PeekSave(const char* path, int& day, int& gold) {
    std::ifstream f(path);
    if (!f) return false;
    day = 0; gold = 0;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string tag;
        if (!(ss >> tag)) continue;
        if (tag == "day")  ss >> day;
        if (tag == "gold") ss >> gold;
        if (tag == "playerpos") break;   // header tags live at the top
    }
    return true;
}

bool SaveGame(const GameState& gs, const char* path) {
    const Content& c = gs.content;
    std::ofstream f(path);
    if (!f) return false;

    f << "OWSAVE " << SAVE_VERSION << '\n';
    f << "gold " << gs.gold << '\n';
    f << "day " << gs.day << '\n';
    f << "clock " << gs.dayTimer << '\n';   // hour of the day (V44)
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
          << ' ' << gs.questTown << ' ' << gs.questProgress
          << ' ' << gs.questDays << ' ' << gs.questEscort << '\n';   // V59/V69

    // Burned-out bandit dens (H2) — live ones are recreated from the map.
    for (int l = 0; l < (int)gs.lairs.size(); ++l)
        if (!gs.lairs[l].alive) f << "lairdead " << l << '\n';

    // The claimed crown (F3).
    if (gs.crowned) f << "crowned 1\n";
    if (gs.renown != 0 || gs.honor != 0)
        f << "fame " << gs.renown << ' ' << gs.honor << '\n';
    if (gs.hintsSeen != 0) f << "hints " << gs.hintsSeen << '\n';
    for (int t = 0; t < (int)gs.wounded.size() && t < c.troops.size(); ++t)
        if (gs.wounded[t] > 0)
            f << "wounded " << c.troops[t].id << ' ' << gs.wounded[t] << '\n';
    if (gs.feastTown >= 0 && gs.feastFaction >= 0 &&
        gs.feastFaction < c.factions.size())
        f << "feast " << gs.feastTown << ' ' << c.factions[gs.feastFaction].id
          << ' ' << gs.feastDays << ' ' << (gs.feastAttended ? 1 : 0) << '\n';
    for (const std::string& g : gs.feastGuests)   // seated lords (V38)
        f << "guest " << g << '\n';
    if (gs.spouseFaction >= 0 && gs.spouseFaction < c.factions.size())
        f << "spouse " << c.factions[gs.spouseFaction].id << ' '
          << gs.spouseName << '\n';
    for (const auto& p : gs.lordOpinion)
        if (p.second != 0)
            f << "lop " << p.first << ' ' << p.second << '\n';
    for (const auto& pl : gs.capturedLords)
        if (pl.second >= 0 && pl.second < c.factions.size())
            f << "plord " << pl.first << ' ' << c.factions[pl.second].id << '\n';
    if (gs.siegeCampTown >= 0)
        f << "scamp " << gs.siegeCampTown << ' ' << gs.siegeCampPrep << ' '
          << gs.siegeCampDays << '\n';

    // Standing duties (K5).
    if (gs.musterTown >= 0)
        f << "muster " << gs.musterTown << ' ' << gs.musterDays << '\n';
    if (gs.lordsRally)
        f << "rally " << gs.lordsRallyPos.x << ' ' << gs.lordsRallyPos.y << ' '
          << gs.lordsRallyDays << '\n';

    // The chronicle (V50): free text, one tag per line.
    for (const std::string& ch : gs.chronicle)
        f << "chron " << ch << '\n';

    // The storm (V62).
    f << "storm " << gs.stormPos.x << ' ' << gs.stormPos.y << ' '
      << gs.stormVel.x << ' ' << gs.stormVel.y << '\n';

    // The loan (V84).
    if (gs.debt > 0) f << "debt " << gs.debt << ' ' << gs.debtDays << '\n';

    // The destrier (V82).
    if (gs.warhorse) f << "warhorse 1\n";

    // The tax lever (V55).
    if (gs.taxRate != 1) f << "tax " << gs.taxRate << '\n';

    // Hungry days on the march (V37).
    if (gs.hungryDays > 0) f << "hungry " << gs.hungryDays << '\n';

    // A running mercenary contract (V29).
    if (gs.mercParty >= 0 && gs.mercDays > 0)
        f << "merc " << gs.mercParty << ' ' << gs.mercDays << '\n';

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
        if (!gs.towns[t].fiefLord.empty())
            f << "fief " << t << ' ' << gs.towns[t].fiefLord << '\n';
        f << "rpool " << t << ' ' << gs.towns[t].recruitPool << '\n';
        if (t < (int)gs.bankAt.size() && gs.bankAt[t] > 0)
            f << "bank " << t << ' ' << gs.bankAt[t] << '\n';
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
        if (t < (int)gs.enterpriseLvl.size() && gs.enterpriseLvl[t] > 1)
            f << "elvl " << t << ' ' << gs.enterpriseLvl[t] << '\n';   // (V49)
        if (gs.towns[t].fortified) f << "fort " << t << '\n';          // (V51)
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
        if (p.caravan) {
            f << "cparty " << c.factions[p.faction].id << ' '
              << p.pos.x << ' ' << p.pos.y << ' ' << p.caravanTo << '\n';
            for (int g = 0; g < (int)p.cargo.size() && g < c.goods.size(); ++g)
                if (p.cargo[g] > 0)
                    f << "ccargo " << c.goods[g].id << ' ' << p.cargo[g] << '\n';
            if (p.cargoCost != 0) f << "ccost " << p.cargoCost << '\n';
        }
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
        else if (tag == "clock") ss >> gs.dayTimer;
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
        } else if (tag == "bank") {
            int idx = -1, v = 0;
            ss >> idx >> v;
            if (idx >= 0 && idx < (int)gs.towns.size() && v > 0) {
                if ((int)gs.bankAt.size() < (int)gs.towns.size())
                    gs.bankAt.resize(gs.towns.size(), 0);
                gs.bankAt[idx] = v;
            }
        } else if (tag == "rpool") {
            int idx = -1, v = 0;
            ss >> idx >> v;
            if (idx >= 0 && idx < (int)gs.towns.size())
                gs.towns[idx].recruitPool = v;
        } else if (tag == "fief") {
            int idx = -1; std::string name;
            ss >> idx >> name;
            if (idx >= 0 && idx < (int)gs.towns.size() && !name.empty())
                gs.towns[idx].fiefLord = name;
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
        } else if (tag == "fame") {
            ss >> gs.renown >> gs.honor;
        } else if (tag == "hints") {
            ss >> gs.hintsSeen;
        } else if (tag == "wounded") {
            std::string id; int n = 0;
            ss >> id >> n;
            const int t = c.troops.find(id.c_str());
            if (t >= 0 && n > 0) {
                if ((int)gs.wounded.size() < c.troops.size())
                    gs.wounded.assign(c.troops.size(), 0);
                gs.wounded[t] = n;
            }
        } else if (tag == "feast") {
            std::string fid; int att = 0;
            ss >> gs.feastTown >> fid >> gs.feastDays >> att;
            gs.feastFaction  = c.factions.find(fid.c_str());
            gs.feastAttended = att != 0;
            if (gs.feastFaction < 0) gs.feastTown = -1;
        } else if (tag == "plord") {
            std::string name, fid;
            ss >> name >> fid;
            const int fh = c.factions.find(fid.c_str());
            if (!name.empty() && fh >= 0)
                gs.capturedLords.push_back({ name, fh });
        } else if (tag == "scamp") {
            ss >> gs.siegeCampTown >> gs.siegeCampPrep >> gs.siegeCampDays;
        } else if (tag == "lop") {
            std::string name; int v = 0;
            ss >> name >> v;
            if (!name.empty()) LordOpinion(gs, name) = v;
        } else if (tag == "spouse") {
            std::string fid;
            ss >> fid >> gs.spouseName;
            gs.spouseFaction = c.factions.find(fid.c_str());
        } else if (tag == "crowned") {
            int v = 0;
            ss >> v;
            gs.crowned = v != 0;
        } else if (tag == "quest") {
            std::string qid, fid;
            ss >> qid >> fid >> gs.questTown >> gs.questProgress;
            if (!(ss >> gs.questDays)) gs.questDays = 0;   // old saves: no clock
            if (!(ss >> gs.questEscort)) gs.questEscort = -1;
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
        } else if (tag == "guest") {
            std::string g;
            ss >> g;
            if (!g.empty()) gs.feastGuests.push_back(g);
        } else if (tag == "debt") {
            ss >> gs.debt >> gs.debtDays;
        } else if (tag == "warhorse") {
            int v = 0; ss >> v; gs.warhorse = v != 0;
        } else if (tag == "storm") {
            ss >> gs.stormPos.x >> gs.stormPos.y >> gs.stormVel.x >> gs.stormVel.y;
        } else if (tag == "tax") {
            ss >> gs.taxRate;
        } else if (tag == "chron") {
            std::string rest;
            std::getline(ss, rest);
            if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
            if (!rest.empty()) gs.chronicle.push_back(rest);
        } else if (tag == "hungry") {
            ss >> gs.hungryDays;
        } else if (tag == "merc") {
            ss >> gs.mercParty >> gs.mercDays;
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
        } else if (tag == "fort") {
            int idx = -1;
            ss >> idx;
            if (idx >= 0 && idx < (int)gs.towns.size()) gs.towns[idx].fortified = true;
        } else if (tag == "elvl") {
            int idx = -1, v = 1;
            ss >> idx >> v;
            if ((int)gs.enterpriseLvl.size() < (int)gs.towns.size())
                gs.enterpriseLvl.assign(gs.towns.size(), 1);
            if (idx >= 0 && idx < (int)gs.enterpriseLvl.size())
                gs.enterpriseLvl[idx] = v;
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
        } else if (tag == "ccost") {
            int v = 0; ss >> v;
            if (cur && cur->caravan) cur->cargoCost = v;
        } else if (tag == "ccargo") {
            std::string gid; int n = 0;
            ss >> gid >> n;
            const int g = c.goods.find(gid.c_str());
            if (cur && cur->caravan && g >= 0 && n > 0) {
                if ((int)cur->cargo.size() < c.goods.size())
                    cur->cargo.assign(c.goods.size(), 0);
                cur->cargo[g] = n;
            }
        }
    }
    gs.screen = Screen::Campaign;
    RefreshWarMarkups(gs);   // saved diplomacy prices the shelves (V31)
    return true;
}
