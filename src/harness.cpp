#include "harness.h"
#include "bridge.h"
#include "save.h"
#include "town/town.h"
#include "world.h"
#include "campaign/campaign.h"
#include "battle/battle.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// ---------------------------------------------------------------------------
// Headless script harness. Drives the exact same simulation code as windowed
// play (CampaignUpdate / SettlementUpdate / BattleUpdate) via input-intent
// structs, at a fixed timestep, printing state on demand. No window, no
// rendering, deterministic under `seed`.
//
// Command reference (one per line; '#' starts a comment):
//   seed N              deterministic run (SetRandomSeed)
//   state               dump current game state
//   tick T              advance T seconds with neutral input (battle still runs;
//                       the overworld is frozen unless travelling/waiting)
//   quit                stop (end-of-file also stops)
// campaign:
//   walk DX DY T        travel in direction (DX,DY) for T seconds (time flows)
//   hunt T              auto-steer toward the nearest hostile party for up to
//                       T seconds; stops early when a battle starts
//   wait T              stand still and let time pass for T seconds
//   enter N             enter settlement index N     |  leave
//   recruit SLOT [N]    recruit N (default 1) troops from roster slot
//   join 1|2            join the nearest skirmish on side a / b
// battle:
//   bmove F R T         move (forward F, strafe-right R; each -1..1) for T secs
//   look DX DY          turn the camera by a mouse delta (pixels)
//   attack up|down|left|right   ready, aim and release one swing
//   block on|off        hold / drop the guard (persists across ticks)
//   swap | menu | formation 1-4 | ranks + | ranks -
// ---------------------------------------------------------------------------

namespace {

constexpr float STEP = 1.0f / 60.0f;   // fixed simulation timestep

struct Harness {
    GameState gs;
    bool      blockHeld = false;   // battle guard persists across ticks
    bool      battleLive = false;  // BattleInit has run for the current battle
    bool      townLive   = false;  // TownInit has run for the current settlement

    // One fixed step of the whole game, mirroring main.cpp's screen routing.
    void Step(const CampaignInput& cin, const BattleInput& bin) {
        switch (gs.screen) {
            case Screen::Title:   // harness skips the menu; play starts direct
            case Screen::Campaign:
            case Screen::BattleResult:
                CampaignUpdate(gs, STEP, cin);
                if (gs.screen == Screen::Battle && !battleLive) {
                    BattleInit(gs.content, MakeBattleSetup(gs));
                    battleLive = true;
                }
                if (gs.screen == Screen::Settlement && !townLive) {
                    TownInit(gs);
                    townLive = true;
                }
                break;
            case Screen::Settlement:
                if (!TownUpdate(gs, STEP, bin, cin)) townLive = false;
                if (gs.screen == Screen::Battle && !battleLive) {   // arena bout
                    BattleInit(gs.content, MakeBattleSetup(gs));
                    battleLive = true;
                }
                break;
            case Screen::Market:
                MarketUpdate(gs, cin);
                if (gs.screen != Screen::Market) townLive = false;   // re-init streets
                break;
            case Screen::Dialogue:
                DialogueUpdate(gs, cin);   // the town scene stays live behind it
                break;
            case Screen::Settings:
                SettingsUpdate(gs, cin);
                break;
            case Screen::Kingdom:
                KingdomUpdate(gs, cin);
                break;
            case Screen::Party:
                PartyUpdate(gs, cin);
                break;
            case Screen::Inventory:
                InventoryUpdate(gs, cin);
                break;
            case Screen::Character:
                CharacterUpdate(gs, cin);
                break;
            case Screen::Victory:
                VictoryUpdate(gs, cin);
                break;
            case Screen::Battle: {
                BattleOutcome out;
                BattleInput b = bin;
                b.block = blockHeld || bin.block;
                if (!BattleUpdate(gs.content, STEP, b, out)) {
                    gs.battleWon    = out.won;
                    gs.playerLosses = out.playerLosses;
                    gs.allyLosses   = out.allyLosses;
                    gs.enemyLosses   = out.enemyLosses;
                    gs.battleHorses  = out.horsesTaken;
                    gs.battleYielded  = out.enemySurrendered;
                    gs.battleSlewLord = out.slewLord;
                    gs.screen = Screen::BattleResult;
                    battleLive = false;
                    blockHeld = false;
                }
                break;
            }
        }
    }

    void Ticks(float seconds, const CampaignInput& cin, const BattleInput& bin) {
        const int n = (int)(seconds / STEP + 0.5f);
        for (int i = 0; i < n; ++i) Step(cin, bin);
    }

    // Steer toward the nearest live hostile party each step until a battle
    // starts (or the time budget runs out). Prefers ordinary parties over
    // lords' hosts — scripts hunting for a fight rarely want a 120-man army.
    void Hunt(float seconds) {
        const int n = (int)(seconds / STEP + 0.5f);
        for (int i = 0; i < n && gs.screen == Screen::Campaign; ++i) {
            int best = -1, bestLord = -1;
            float bestD = 1e9f, bestLordD = 1e9f;
            for (int p = 0; p < (int)gs.parties.size(); ++p) {
                const Party& e = gs.parties[p];
                if (!e.alive || e.engaged) continue;
                if (!AtWar(gs, e.faction, gs.content.playerFaction)) continue;
                const float d = Vector2Distance(e.pos, gs.player.pos);
                if (e.lord.empty()) { if (d < bestD) { bestD = d; best = p; } }
                else                { if (d < bestLordD) { bestLordD = d; bestLord = p; } }
            }
            if (best < 0) best = bestLord;   // only lords left to fight
            CampaignInput cin;
            if (best >= 0)
                cin.move = Vector2Subtract(gs.parties[best].pos, gs.player.pos);
            else
                cin.wait = true;   // nothing to hunt yet; let the world breathe
            Step(cin, BattleInput{});
        }
    }

    const char* ScreenName() const {
        switch (gs.screen) {
            case Screen::Title:        return "Title";
            case Screen::Background:   return "Background";
            case Screen::LoadMenu:     return "LoadMenu";
            case Screen::Campaign:     return "Campaign";
            case Screen::Settlement:   return "Settlement";
            case Screen::Market:       return "Market";
            case Screen::Dialogue:     return "Dialogue";
            case Screen::Settings:     return "Settings";
            case Screen::Kingdom:      return "Kingdom";
            case Screen::Party:        return "Party";
            case Screen::Inventory:    return "Inventory";
            case Screen::Character:    return "Character";
            case Screen::Battle:       return "Battle";
            case Screen::BattleResult: return "BattleResult";
            case Screen::Victory:      return "Victory";
        }
        return "?";
    }

    void DumpState() const {
        const Content& c = gs.content;
        std::printf("screen=%s day=%d gold=%d pos=(%.0f,%.0f) party=%d timeFlowing=%d "
                    "terrain=%s%s speed=%.2f\n",
                    ScreenName(), gs.day, gs.gold, gs.player.pos.x, gs.player.pos.y,
                    gs.player.totalTroops(), gs.timeFlowing ? 1 : 0,
                    WorldTerrainName(WorldTerrainAt(gs.content.map, gs.player.pos)),
                    OnRoad(gs, gs.player.pos) ? " road" : "",
                    TravelSpeedFactor(gs, gs.player.pos));
        std::printf("hero: level=%d xp=%d points=%d renown=%d honor=%d cap=%d "
                    "hints=%u attrs=[",
                    gs.playerHero.level, gs.playerHero.xp, gs.playerHero.attrPoints,
                    gs.renown, gs.honor, PartyCap(gs), gs.hintsSeen);
        for (int a = 0; a < c.attributes.size(); ++a)
            std::printf("%s%s=%d", a ? " " : "", c.attributes[a].id.c_str(),
                        a < (int)gs.playerHero.attributes.size()
                            ? gs.playerHero.attributes[a] : 0);
        std::printf("]\n");
        std::printf("troops:");
        for (int t = 0; t < (int)gs.player.troopCounts.size(); ++t)
            if (gs.player.troopCounts[t] > 0)
                std::printf(" %s=%d(xp%d)", c.troops[t].id.c_str(), gs.player.troopCounts[t],
                            t < (int)gs.troopXp.size() ? gs.troopXp[t] : 0);
        std::printf("\n");
        int captives = 0;
        for (int n : gs.prisoners) captives += n;
        if (captives > 0) std::printf("captives=%d\n", captives);
        if (gs.hungryDays > 0) std::printf("hungry=%d\n", gs.hungryDays);
        if (gs.warhorse) std::printf("warhorse=1\n");
        if (gs.debt > 0)
            std::printf("debt: %d days=%.0f\n", gs.debt, gs.debtDays);
        std::printf("storm: pos=(%.0f,%.0f) inside=%d\n", gs.stormPos.x,
                    gs.stormPos.y, InStorm(gs, gs.player.pos) ? 1 : 0);
        {
            std::string perks;
            for (int t = 0; t < c.troops.size() &&
                            t < (int)gs.player.troopCounts.size(); ++t)
                if (gs.player.troopCounts[t] > 0 && !c.troops[t].perk.empty())
                    perks += (perks.empty() ? "" : " ") + c.troops[t].perk;
            if (!perks.empty()) std::printf("perks: %s\n", perks.c_str());
        }
        for (int ci = (int)gs.chronicle.size() - 3; ci < (int)gs.chronicle.size(); ++ci)
            if (ci >= 0) std::printf("chron: %s\n", gs.chronicle[ci].c_str());
        if (!gs.resultText.empty())
            std::printf("result=\"%s\"\n", gs.resultText.c_str());
        for (int t = 0; t < (int)gs.towns.size(); ++t) {
            const Town& tw = gs.towns[t];
            std::printf("town %d: %s owner=%s garrison=%d prosper=%d pool=%d%s%s%s dist=%.0f\n",
                        t, tw.name.c_str(),
                        (tw.owner >= 0 && tw.owner < c.factions.size())
                            ? c.factions[tw.owner].id.c_str() : "none",
                        tw.garrisonSize(), tw.prosperity, tw.recruitPool,
                        tw.fiefLord.empty() ? "" : " fief=",
                        tw.fiefLord.c_str(),
                        tw.fortified ? " FORT" : "",
                        Vector2Distance(tw.pos, gs.player.pos));
        }
        for (int l = 0; l < (int)gs.lairs.size(); ++l)
            std::printf("lair %d: faction=%s pos=(%.0f,%.0f) alive=%d\n", l,
                        c.factions.valid(gs.lairs[l].faction)
                            ? c.factions[gs.lairs[l].faction].id.c_str() : "?",
                        gs.lairs[l].pos.x, gs.lairs[l].pos.y,
                        gs.lairs[l].alive ? 1 : 0);
        for (int i = 0; i < (int)gs.parties.size(); ++i) {
            const Party& p = gs.parties[i];
            if (!p.alive) continue;
            int freight = 0;
            for (int q : p.cargo) freight += q;
            std::printf("party %d: faction=%s%s%s%s pos=(%.0f,%.0f) troops=%d state=%s fatigue=%.0f engaged=%d dist=%.0f\n",
                        i, c.factions[p.faction].id.c_str(),
                        p.caravan ? TextFormat(" caravan(cargo=%d)", freight) : "",
                        p.lord.empty() ? "" : " lord=", p.lord.c_str(),
                        p.pos.x, p.pos.y, p.totalTroops(), PartyStateName(p.state),
                        p.fatigue, p.engaged ? 1 : 0,
                        Vector2Distance(p.pos, gs.player.pos));
        }
        for (int g = 0; g < c.goods.size() && g < (int)gs.goods.size(); ++g)
            if (gs.goods[g] > 0)
                std::printf("good: %s=%d\n", c.goods[g].id.c_str(), gs.goods[g]);
        if (gs.screen == Screen::Dialogue) {
            std::printf("dialogue: %s\n", gs.dialogueName.c_str());
            for (const std::string& l : gs.dialogueLines)
                std::printf("  \"%s\"\n", l.c_str());
        }
        if (gs.crowned) std::printf("crowned=1\n");
        if (gs.musterTown >= 0)
            std::printf("muster: town=%d days=%.1f\n", gs.musterTown, gs.musterDays);
        if (gs.lordsRally)
            std::printf("rally: pos=(%.0f,%.0f) days=%.1f\n", gs.lordsRallyPos.x,
                        gs.lordsRallyPos.y, gs.lordsRallyDays);
        if (gs.liege >= 0 && gs.liege < c.factions.size())
            std::printf("liege=%s\n", c.factions[gs.liege].id.c_str());
        if (gs.mercParty >= 0 && gs.mercDays > 0)
            std::printf("merc: party=%d days=%.1f\n", gs.mercParty, gs.mercDays);
        if (gs.activeQuest >= 0 && gs.activeQuest < c.quests.size())
            std::printf("quest: %s progress=%d target=%d days=%.0f\n",
                        c.quests[gs.activeQuest].id.c_str(), gs.questProgress,
                        gs.questTown, gs.questDays);
        for (int f = 0; f < c.factions.size() && f < (int)gs.relations.size(); ++f)
            if (gs.relations[f] != 0)
                std::printf("relation: %s=%+d\n", c.factions[f].id.c_str(), gs.relations[f]);
        for (int t = 0; t < (int)gs.enterpriseAt.size() && t < (int)gs.towns.size(); ++t)
            if (c.enterprises.valid(gs.enterpriseAt[t]))
                std::printf("enterprise: %s in %s lvl=%d\n",
                            c.enterprises[gs.enterpriseAt[t]].id.c_str(),
                            gs.towns[t].name.c_str(),
                            t < (int)gs.enterpriseLvl.size() ? gs.enterpriseLvl[t] : 1);
        if (gs.screen == Screen::Market && gs.currentSettlement >= 0) {
            const Town& tw = gs.towns[gs.currentSettlement];
            int carried = 0;
            for (int q : gs.goods) carried += q;
            std::printf("saddlebags=%d/%d warmarkup=%d\n", carried, GOODS_CAP,
                        tw.warMarkup);
            for (int g = 0; g < c.goods.size() && g < (int)tw.stock.size(); ++g)
                std::printf("market: %s stock=%d offset=%d buy=%d sell=%d\n",
                            c.goods[g].id.c_str(), tw.stock[g], tw.priceOffset[g],
                            MarketBuyPrice(c, tw, g), MarketSellPrice(c, tw, g));
        }
        {
            int hurt = 0;
            for (int w : gs.wounded) hurt += w;
            if (hurt > 0) std::printf("wounded=%d\n", hurt);
        }
        for (int bi = 0; bi < (int)gs.bankAt.size(); ++bi)
            if (gs.bankAt[bi] > 0)
                std::printf("bank: town=%d gold=%d\n", bi, gs.bankAt[bi]);
        {
            const DayLedger L = ComputeLedger(gs);
            std::printf("tax=%d\n", gs.taxRate);
            std::printf("purse: income=%d enterprise=%d wages=%d retainers=%d "
                        "garrisons=%d net=%+d\n", L.income, L.enterprise,
                        L.wages, L.lordPay, L.garrisonPay, L.net());
        }
        if (gs.siegePrompt >= 0)
            std::printf("siegeprompt: town=%d\n", gs.siegePrompt);
        if (gs.siegeCampTown >= 0)
            std::printf("siegecamp: town=%d prep=%d days=%.1f\n",
                        gs.siegeCampTown, gs.siegeCampPrep, gs.siegeCampDays);
        if (gs.feastTown >= 0 && gs.feastDays > 0)
            std::printf("feast: town=%d faction=%s days=%.1f attended=%d guests=%d\n",
                        gs.feastTown,
                        c.factions.valid(gs.feastFaction)
                            ? c.factions[gs.feastFaction].id.c_str() : "?",
                        gs.feastDays, gs.feastAttended ? 1 : 0,
                        (int)gs.feastGuests.size());
        for (const auto& pl : gs.capturedLords)
            std::printf("plord: %s of %s\n", pl.first.c_str(),
                        c.factions.valid(pl.second)
                            ? c.factions[pl.second].id.c_str() : "?");
        for (const auto& p : gs.lordOpinion)
            if (p.second != 0)
                std::printf("lop: %s=%+d (eff %+d)\n", p.first.c_str(), p.second,
                            p.second + gs.honor);
        if (gs.spouseFaction >= 0)
            std::printf("spouse: %s of %s\n", gs.spouseName.c_str(),
                        c.factions.valid(gs.spouseFaction)
                            ? c.factions[gs.spouseFaction].id.c_str() : "?");
        for (const auto& p : gs.companionGear) {
            if (p.first < 0 || p.first >= c.troops.size()) continue;
            std::printf("cgear: %s", c.troops[p.first].id.c_str());
            for (int s = 0; s < EQUIP_SLOT_COUNT; ++s) {
                if (s == (int)EquipSlot::Weapon) continue;
                if (c.armor.valid(p.second.slots[s]))
                    std::printf(" %s", c.armor[p.second.slots[s]].id.c_str());
            }
            std::printf(" |");
            for (int w : p.second.weapons)
                if (c.weapons.valid(w)) std::printf(" %s", c.weapons[w].id.c_str());
            std::printf("\n");
        }
        for (const InvItem& it : gs.inventory)
            std::printf("item: %s %s at (%d,%d)\n", it.isWeapon ? "weapon" : "armor",
                        it.isWeapon ? c.weapons[it.handle].id.c_str()
                                    : c.armor[it.handle].id.c_str(), it.x, it.y);
        // Live diplomacy: every pair that differs from the base relations.
        {
            const int nf = c.factions.size();
            if ((int)gs.hostile.size() == nf * nf)
                for (int a = 0; a < nf; ++a)
                    for (int b = a + 1; b < nf; ++b) {
                        const bool now  = gs.hostile[(size_t)a * nf + b] != 0;
                        const bool base = AreFactionsHostile(c, a, b);
                        const float tr  = gs.truceDays[(size_t)a * nf + b];
                        if (now != base || tr > 0)
                            std::printf("diplo: %s vs %s %s truce=%.1f\n",
                                        c.factions[a].id.c_str(), c.factions[b].id.c_str(),
                                        now ? "war" : "peace", tr);
                    }
        }
        for (const AISiege& sg : gs.aiSieges)
            std::printf("aisiege: party=%d town=%d timer=%.1f\n", sg.party, sg.town, sg.timer);
        for (int s = 0; s < (int)gs.skirmishes.size(); ++s)
            std::printf("skirmish %d: a=%d b=%d timer=%.1f pos=(%.0f,%.0f)\n",
                        s, gs.skirmishes[s].a, gs.skirmishes[s].b,
                        gs.skirmishes[s].timer, gs.skirmishes[s].pos.x, gs.skirmishes[s].pos.y);
        if (gs.screen == Screen::Battle) {
            const BattleView v = GetBattleView();
            const char* wname = c.weapons.valid(v.heroWeapon)
                                    ? c.weapons[v.heroWeapon].id.c_str() : "none";
            std::printf("battle: heroPos=(%.2f,%.2f,%.2f) yaw=%.3f pitch=%.3f "
                        "hp=%.0f/%.0f weapon=%s mounted=%d horse=%.0f allies=%d enemies=%d "
                        "arrows=%d wall=%d climbs=%d rain=%d night=%d kills=%d horses=%d order=%s form=%s anchordist=%.1f over=%d won=%d foe=%s banners=%d/%d reserves=%d/%d\n",
                        v.heroPos.x, v.heroPos.y, v.heroPos.z, v.heroYaw, v.heroPitch,
                        v.heroHp, v.heroMaxHp, wname, v.heroMounted ? 1 : 0, v.heroHorseHp,
                        v.aliveAllies, v.aliveEnemies, v.arrowsInFlight, v.wallDefenders,
                        v.climbPoints, v.raining ? 1 : 0, v.night ? 1 : 0, v.heroKills,
                        v.looseHorses, v.order, v.formation, v.ownAvgDistToAnchor,
                        v.over ? 1 : 0, v.won ? 1 : 0,
                        v.enemyName.empty() ? "?" : v.enemyName.c_str(),
                        v.bannerOwn ? 1 : 0, v.bannerEnemy ? 1 : 0,
                        v.reservesOwn, v.reservesEnemy);
        }
        if (gs.screen == Screen::Settlement) {
            const TownView v = GetTownView();
            std::printf("settlement=%d (%s) heroPos=(%.1f,%.1f) yaw=%.2f "
                        "tavern=(%.1f,%.1f) atTavern=%d inside=%d npcs=%d\n",
                        gs.currentSettlement, gs.towns[gs.currentSettlement].name.c_str(),
                        v.heroPos.x, v.heroPos.z, v.heroYaw,
                        v.tavernPos.x, v.tavernPos.z, v.atTavern ? 1 : 0,
                        v.inside ? 1 : 0, v.npcs);
        }
        std::fflush(stdout);
    }
};

// Ready, aim and release one directional swing: press, hold ~0.4 s while
// feeding aim motion so DirFromMotion picks the direction, then release.
void DoAttack(Harness& h, const std::string& dir) {
    Vector2 aim{ 0, 0 };
    if (dir == "up")    aim = { 0, -20 };
    else if (dir == "down")  aim = { 0, 20 };
    else if (dir == "left")  aim = { -20, 0 };
    else if (dir == "right") aim = { 20, 0 };

    // Warband rule (U5): flick the mouse FIRST, then click — the direction
    // locks at the moment of the press.
    BattleInput flick;
    flick.lookDelta = aim;
    h.Step(CampaignInput{}, flick);

    BattleInput press;
    press.attackPress = true;
    press.lookDelta = { 0, 0 };
    h.Step(CampaignInput{}, press);

    BattleInput hold;
    for (int i = 0; i < 22; ++i) h.Step(CampaignInput{}, hold);

    BattleInput release;
    release.attackRelease = true;
    h.Step(CampaignInput{}, release);
}

}  // namespace

int RunScript(const char* path) {
    std::ifstream file;
    std::istream* in = &std::cin;
    if (std::strcmp(path, "-") != 0) {
        file.open(path);
        if (!file) { std::fprintf(stderr, "harness: cannot open %s\n", path); return 1; }
        in = &file;
    }

    Harness h;
    LoadDefaultContent(h.gs.content);
    CampaignInit(h.gs);

    std::string line;
    while (std::getline(*in, line)) {
        if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        std::istringstream ss(line);
        std::string cmd;
        if (!(ss >> cmd)) continue;
        std::printf("> %s\n", line.c_str());

        if (cmd == "quit") break;
        else if (cmd == "seed") { unsigned s = 1; ss >> s; SetRandomSeed(s); }
        else if (cmd == "state") h.DumpState();
        else if (cmd == "tick") {
            float t = 1; ss >> t;
            h.Ticks(t, CampaignInput{}, BattleInput{});
        } else if (cmd == "walk") {
            CampaignInput cin; float t = 1;
            ss >> cin.move.x >> cin.move.y >> t;
            h.Ticks(t, cin, BattleInput{});
        } else if (cmd == "hunt") {
            float t = 10; ss >> t;
            h.Hunt(t);
        } else if (cmd == "wait") {
            float t = 1; ss >> t;
            CampaignInput cin; cin.wait = true;
            h.Ticks(t, cin, BattleInput{});
        } else if (cmd == "enter") {
            CampaignInput cin; ss >> cin.clickSettlement;
            h.Step(cin, BattleInput{});
        } else if (cmd == "leave") {
            CampaignInput cin; cin.leaveSettlement = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "garrison" || cmd == "ungarrison") {
            // Man your walls (S2): move N soldiers party<->garrison.
            int n = 1; ss >> n;
            for (int i = 0; i < n; ++i) {
                CampaignInput cin;
                (cmd == "garrison" ? cin.garrisonOne : cin.ungarrisonOne) = true;
                h.Step(cin, BattleInput{});
            }
        } else if (cmd == "tavern") {
            // Stand at the tavern door (Q1) — recruiting requires it.
            if (h.gs.screen == Screen::Settlement) TownGoTavern(h.gs);
        } else if (cmd == "recruit") {
            int slot = 0, n = 1; ss >> slot >> n;
            for (int i = 0; i < n; ++i) {
                CampaignInput cin; cin.recruitSlot = slot;
                h.Step(cin, BattleInput{});
            }
        } else if (cmd == "char") {
            CampaignInput cin;
            if (h.gs.screen == Screen::Character) cin.leaveSettlement = true;
            else                                  cin.openCharacter = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "spend") {
            CampaignInput cin; ss >> cin.spendAttr;
            h.Step(cin, BattleInput{});
        } else if (cmd == "inv") {
            CampaignInput cin;
            if (h.gs.screen == Screen::Inventory) cin.leaveSettlement = true;
            else                                  cin.openInventory = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "target") {
            CampaignInput cin; cin.invCycleTarget = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "equip") {
            CampaignInput cin;
            ss >> cin.invCellX >> cin.invCellY;
            cin.invEquip = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "party") {
            CampaignInput cin;
            if (h.gs.screen == Screen::Party) cin.leaveSettlement = true;
            else                              cin.openParty = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "dismiss") {
            int slot = 0, n = 1; ss >> slot >> n;
            for (int i = 0; i < n; ++i) {
                CampaignInput cin; cin.dismissSlot = slot;
                h.Step(cin, BattleInput{});
            }
        } else if (cmd == "upgrade") {
            int slot = 0, n = 1; ss >> slot >> n;
            for (int i = 0; i < n; ++i) {
                CampaignInput cin; cin.upgradeSlot = slot;
                h.Step(cin, BattleInput{});
            }
        } else if (cmd == "interact") {
            CampaignInput cin; cin.interact = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "ransom") {
            CampaignInput cin; cin.ransom = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "save" || cmd == "load") {
            std::string p;
            if (!(ss >> p)) p = DefaultSavePath();
            const bool ok = (cmd == "save") ? SaveGame(h.gs, p.c_str())
                                            : LoadGame(h.gs, p.c_str());
            if (cmd == "load" && ok) {
                // Any in-flight battle/town scene died with the old world.
                h.battleLive = false;
                h.townLive   = false;
            }
            std::printf("%s %s: %s\n", cmd.c_str(), p.c_str(), ok ? "ok" : "FAILED");
        } else if (cmd == "market") {
            CampaignInput cin;
            if (h.gs.screen == Screen::Market) cin.leaveSettlement = true;
            else                               cin.openMarket = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "buy" || cmd == "sell") {
            std::string id; int n = 1;
            ss >> id >> n;
            const int g = h.gs.content.goods.find(id.c_str());
            if (g < 0) {
                std::fprintf(stderr, "harness: unknown good '%s'\n", id.c_str());
            } else for (int i = 0; i < n; ++i) {
                CampaignInput cin;
                (cmd == "buy" ? cin.buyGood : cin.sellGood) = g;
                h.Step(cin, BattleInput{});
            }
        } else if (cmd == "buyarm") {
            // Arms from the forge (O4): buy today's armour or weapon piece.
            std::string kind; ss >> kind;
            CampaignInput cin;
            cin.buyGood = h.gs.content.goods.size() + (kind == "weapon" ? 1 : 0);
            h.Step(cin, BattleInput{});
        } else if (cmd == "settings") {
            CampaignInput cin;
            if (h.gs.screen == Screen::Settings) cin.leaveSettlement = true;
            else                                 cin.openSettings = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "setopt") {
            CampaignInput cin; ss >> cin.settingsRow;
            cin.settingsRow--;   // 1-based on the command line, like the keys
            h.Step(cin, BattleInput{});
        } else if (cmd == "deposit" || cmd == "withdraw") {
            // The moneylender (V5): move gold in 100s at the open market.
            CampaignInput cin;
            cin.bankMove = cmd == "deposit" ? 100 : -100;
            h.Step(cin, BattleInput{});
        } else if (cmd == "sendcaravan") {
            // Outfit a player trade convoy at the open market (M4).
            CampaignInput cin; cin.sendCaravan = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "siege") {
            // Answer the assault-choice prompt (N1): 1 storm, 2 ladders,
            // 3 tower, 4 walk away.
            CampaignInput cin; ss >> cin.menuChoice;
            h.Step(cin, BattleInput{});
        } else if (cmd == "saveslot") {
            int n = 1; ss >> n;
            std::printf("saveslot %d: %s\n", n,
                        SaveGame(h.gs, SaveSlotPath(n)) ? "ok" : "FAILED");
        } else if (cmd == "loadslot") {
            int n = 1; ss >> n;
            std::printf("loadslot %d: %s\n", n,
                        LoadGame(h.gs, SaveSlotPath(n)) ? "ok" : "FAILED");
        } else if (cmd == "background") {
            // Character creation (N2): apply a background's start directly.
            int b = 0; ss >> b;
            ApplyBackground(h.gs, b);
            std::printf("background %d: %s\n", b, h.gs.resultText.c_str());
        } else if (cmd == "ledger") {
            // Toggle the kingdom ledger (O1) — open from the map, Esc closes.
            CampaignInput cin;
            if (h.gs.screen == Screen::Kingdom) cin.leaveSettlement = true;
            else                                cin.openLedger = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "capture") {
            // Scenario shortcut (O2): put a named lord in the player's train.
            std::string name, fid;
            ss >> name >> fid;
            const int fh = h.gs.content.factions.find(fid.c_str());
            if (fh >= 0) h.gs.capturedLords.push_back({ name, fh });
            std::printf("capture: %s of %s\n", name.c_str(), fid.c_str());
        } else if (cmd == "ransomlords" || cmd == "releaselords") {
            CampaignInput cin;
            (cmd == "ransomlords" ? cin.ransomLords : cin.releaseLords) = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "fame") {
            // Scripting shortcut (M1): set renown/honor directly so scenarios
            // can stand at a chosen reputation without grinding battles.
            ss >> h.gs.renown >> h.gs.honor;
            std::printf("fame: renown=%d honor=%d\n", h.gs.renown, h.gs.honor);
        } else if (cmd == "court") {
            // Open the castle court audience (skips the walk to the keep).
            if (h.gs.screen == Screen::Settlement) {
                TownTalkLord(h.gs);
                h.gs.screen = Screen::Dialogue;
            }
        } else if (cmd == "parley") {
            // Hail the nearest lord party on the map (S4).
            CampaignInput cin; cin.parley = true;
            h.Step(cin, BattleInput{});
            std::printf("parley: %s\n",
                        h.gs.screen == Screen::Dialogue ? h.gs.dialogueName.c_str()
                                                        : h.gs.resultText.c_str());
        } else if (cmd == "talk") {
            // Open a conversation with the nearest NPC (skips the walk-up).
            if (h.gs.screen == Screen::Settlement) {
                TownTalkNearest(h.gs);
                h.gs.screen = Screen::Dialogue;
            }
        } else if (cmd == "topic") {
            CampaignInput cin; ss >> cin.menuChoice;
            h.Step(cin, BattleInput{});
        } else if (cmd == "raid") {
            CampaignInput cin; ss >> cin.clickLair;
            h.Step(cin, BattleInput{});
        } else if (cmd == "rally") {
            CampaignInput cin; cin.rallyLords = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "crown") {
            CampaignInput cin; cin.crown = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "raiselord") {
            CampaignInput cin; cin.raiseLord = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "hire") {
            CampaignInput cin; cin.hire = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "quest") {
            CampaignInput cin; cin.quest = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "swear") {
            CampaignInput cin; cin.swear = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "tournament") {
            std::string bet;
            ss >> bet;
            CampaignInput cin; cin.tournament = true;
            cin.tournamentBet = (bet == "bet");
            h.Step(cin, BattleInput{});
        } else if (cmd == "enterprise") {
            CampaignInput cin; cin.buyEnterprise = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "join") {
            CampaignInput cin; ss >> cin.joinSide;
            h.Step(cin, BattleInput{});
        } else if (cmd == "bmove") {
            BattleInput bin; float t = 1;
            ss >> bin.moveForward >> bin.moveRight >> t;
            h.Ticks(t, CampaignInput{}, bin);
        } else if (cmd == "look") {
            BattleInput bin; ss >> bin.lookDelta.x >> bin.lookDelta.y;
            h.Step(CampaignInput{}, bin);
        } else if (cmd == "attack") {
            std::string dir; ss >> dir;
            DoAttack(h, dir);
        } else if (cmd == "block") {
            std::string v; ss >> v;
            h.blockHeld = (v == "on");
        } else if (cmd == "mount") {
            // Dismount / remount (U11) — Z in the saddle or beside a horse.
            BattleInput bin; bin.mountToggle = true;
            h.Step(CampaignInput{}, bin);
        } else if (cmd == "loan") {
            // Borrow or repay at the moneylender (V84) — L at a town market.
            CampaignInput cin; cin.loan = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "destrier") {
            // Buy the warhorse (V82) — W at a town market.
            CampaignInput cin; cin.buyWarhorse = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "tax") {
            // Cycle the tax lever (V55) — T on the kingdom ledger.
            CampaignInput cin; cin.cycleTax = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "press") {
            // Press a captive into the warband (V36) — party screen R.
            CampaignInput cin; cin.pressPrisoner = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "warcry") {
            // Sound the horn (V66): steady the line, turn routed men.
            BattleInput bin; bin.warCry = true;
            h.Step(CampaignInput{}, bin);
        } else if (cmd == "autoresolve") {
            // Fight it on paper (V41) — N during the horn's grace period.
            BattleInput bin; bin.autoResolve = true;
            h.Step(CampaignInput{}, bin);
        } else if (cmd == "pickup") {
            // Take up a fallen man's weapon (V39) — G in battle.
            BattleInput bin; bin.pickup = true;
            h.Step(CampaignInput{}, bin);
            const BattleView v = GetBattleView();
            std::printf("pickup: weapon=%s\n",
                        h.gs.content.weapons.valid(v.heroWeapon)
                            ? h.gs.content.weapons[v.heroWeapon].id.c_str() : "none");
        } else if (cmd == "kick") {
            // The boot (V33): stagger the man in front through his shield.
            BattleInput bin; bin.kick = true;
            h.Step(CampaignInput{}, bin);
            std::printf("kick: landed=%d\n", GetBattleView().heroKicks);
        } else if (cmd == "swap") {
            BattleInput bin; bin.swapWeapon = true;
            h.Step(CampaignInput{}, bin);
        } else if (cmd == "menu") {
            BattleInput bin; bin.toggleMenu = true;
            h.Step(CampaignInput{}, bin);
        } else if (cmd == "formation") {
            BattleInput bin; ss >> bin.formationSelect;
            h.Step(CampaignInput{}, bin);
        } else if (cmd == "order") {
            // Battlefield orders (M2): hold / follow / charge.
            std::string o; ss >> o;
            BattleInput bin;
            bin.order = o == "hold" ? 1 : o == "follow" ? 2 : 3;
            h.Step(CampaignInput{}, bin);
        } else if (cmd == "ranks") {
            std::string v; ss >> v;
            BattleInput bin; bin.ranksDelta = (v == "+") ? 1 : -1;
            h.Step(CampaignInput{}, bin);
        } else {
            std::fprintf(stderr, "harness: unknown command '%s'\n", cmd.c_str());
        }
    }
    std::printf("harness: done\n");
    return 0;
}
