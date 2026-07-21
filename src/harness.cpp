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
                    gs.enemyLosses  = out.enemyLosses;
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
            case Screen::Campaign:     return "Campaign";
            case Screen::Settlement:   return "Settlement";
            case Screen::Market:       return "Market";
            case Screen::Dialogue:     return "Dialogue";
            case Screen::Settings:     return "Settings";
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
        std::printf("hero: level=%d xp=%d points=%d renown=%d honor=%d cap=%d attrs=[",
                    gs.playerHero.level, gs.playerHero.xp, gs.playerHero.attrPoints,
                    gs.renown, gs.honor, PartyCap(gs));
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
        if (!gs.resultText.empty())
            std::printf("result=\"%s\"\n", gs.resultText.c_str());
        for (int t = 0; t < (int)gs.towns.size(); ++t) {
            const Town& tw = gs.towns[t];
            std::printf("town %d: %s owner=%s garrison=%d prosper=%d dist=%.0f\n", t, tw.name.c_str(),
                        (tw.owner >= 0 && tw.owner < c.factions.size())
                            ? c.factions[tw.owner].id.c_str() : "none",
                        tw.garrisonSize(), tw.prosperity,
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
        if (gs.activeQuest >= 0 && gs.activeQuest < c.quests.size())
            std::printf("quest: %s progress=%d target=%d\n",
                        c.quests[gs.activeQuest].id.c_str(), gs.questProgress,
                        gs.questTown);
        for (int f = 0; f < c.factions.size() && f < (int)gs.relations.size(); ++f)
            if (gs.relations[f] != 0)
                std::printf("relation: %s=%+d\n", c.factions[f].id.c_str(), gs.relations[f]);
        for (int t = 0; t < (int)gs.enterpriseAt.size() && t < (int)gs.towns.size(); ++t)
            if (c.enterprises.valid(gs.enterpriseAt[t]))
                std::printf("enterprise: %s in %s\n",
                            c.enterprises[gs.enterpriseAt[t]].id.c_str(),
                            gs.towns[t].name.c_str());
        if (gs.screen == Screen::Market && gs.currentSettlement >= 0) {
            const Town& tw = gs.towns[gs.currentSettlement];
            int carried = 0;
            for (int q : gs.goods) carried += q;
            std::printf("saddlebags=%d/%d\n", carried, GOODS_CAP);
            for (int g = 0; g < c.goods.size() && g < (int)tw.stock.size(); ++g)
                std::printf("market: %s stock=%d offset=%d buy=%d sell=%d\n",
                            c.goods[g].id.c_str(), tw.stock[g], tw.priceOffset[g],
                            MarketBuyPrice(c, tw, g), MarketSellPrice(c, tw, g));
        }
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
                        "arrows=%d wall=%d order=%s anchordist=%.1f over=%d won=%d\n",
                        v.heroPos.x, v.heroPos.y, v.heroPos.z, v.heroYaw, v.heroPitch,
                        v.heroHp, v.heroMaxHp, wname, v.heroMounted ? 1 : 0, v.heroHorseHp,
                        v.aliveAllies, v.aliveEnemies, v.arrowsInFlight, v.wallDefenders,
                        v.order, v.ownAvgDistToAnchor, v.over ? 1 : 0, v.won ? 1 : 0);
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

    BattleInput press;
    press.attackPress = true;
    press.lookDelta = { 0, 0 };
    h.Step(CampaignInput{}, press);

    BattleInput hold;
    hold.lookDelta = aim;             // aim while winding up (camera turns a bit,
    h.Step(CampaignInput{}, hold);    // exactly like flicking the mouse)
    hold.lookDelta = { 0, 0 };
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
        } else if (cmd == "settings") {
            CampaignInput cin;
            if (h.gs.screen == Screen::Settings) cin.leaveSettlement = true;
            else                                 cin.openSettings = true;
            h.Step(cin, BattleInput{});
        } else if (cmd == "setopt") {
            CampaignInput cin; ss >> cin.settingsRow;
            cin.settingsRow--;   // 1-based on the command line, like the keys
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
