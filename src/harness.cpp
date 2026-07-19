#include "harness.h"
#include "save.h"
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

    // One fixed step of the whole game, mirroring main.cpp's screen routing.
    void Step(const CampaignInput& cin, const BattleInput& bin) {
        switch (gs.screen) {
            case Screen::Campaign:
            case Screen::BattleResult:
                CampaignUpdate(gs, STEP, cin);
                if (gs.screen == Screen::Battle && !battleLive) {
                    BattleInit(gs.content, MakeSetup());
                    battleLive = true;
                }
                break;
            case Screen::Settlement:
                SettlementUpdate(gs, cin);
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

    BattleSetup MakeSetup() const {
        BattleSetup s;
        s.playerTroops = gs.player.troopCounts;
        s.enemyTroops  = gs.parties[gs.battlePartyIndex].troopCounts;
        if (gs.battleAllyIndex >= 0 && gs.battleAllyIndex < (int)gs.parties.size())
            s.allyTroops = gs.parties[gs.battleAllyIndex].troopCounts;
        s.heroLoadout = gs.playerHero.loadout;
        s.heroMaxHp   = gs.playerHero.maxHp;
        s.campaignPos = gs.player.pos;
        return s;
    }

    void Ticks(float seconds, const CampaignInput& cin, const BattleInput& bin) {
        const int n = (int)(seconds / STEP + 0.5f);
        for (int i = 0; i < n; ++i) Step(cin, bin);
    }

    // Steer toward the nearest live hostile party each step until a battle
    // starts (or the time budget runs out).
    void Hunt(float seconds) {
        const int n = (int)(seconds / STEP + 0.5f);
        for (int i = 0; i < n && gs.screen == Screen::Campaign; ++i) {
            int best = -1;
            float bestD = 1e9f;
            for (int p = 0; p < (int)gs.parties.size(); ++p) {
                const Party& e = gs.parties[p];
                if (!e.alive || e.engaged) continue;
                if (!AreFactionsHostile(gs.content, e.faction, gs.content.playerFaction)) continue;
                const float d = Vector2Distance(e.pos, gs.player.pos);
                if (d < bestD) { bestD = d; best = p; }
            }
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
            case Screen::Campaign:     return "Campaign";
            case Screen::Settlement:   return "Settlement";
            case Screen::Battle:       return "Battle";
            case Screen::BattleResult: return "BattleResult";
        }
        return "?";
    }

    void DumpState() const {
        const Content& c = gs.content;
        std::printf("screen=%s gold=%d pos=(%.0f,%.0f) party=%d timeFlowing=%d\n",
                    ScreenName(), gs.gold, gs.player.pos.x, gs.player.pos.y,
                    gs.player.totalTroops(), gs.timeFlowing ? 1 : 0);
        std::printf("troops:");
        for (int t = 0; t < (int)gs.player.troopCounts.size(); ++t)
            if (gs.player.troopCounts[t] > 0)
                std::printf(" %s=%d", c.troops[t].id.c_str(), gs.player.troopCounts[t]);
        std::printf("\n");
        if (!gs.resultText.empty())
            std::printf("result=\"%s\"\n", gs.resultText.c_str());
        for (int i = 0; i < (int)gs.parties.size(); ++i) {
            const Party& p = gs.parties[i];
            if (!p.alive) continue;
            std::printf("party %d: faction=%s pos=(%.0f,%.0f) troops=%d engaged=%d dist=%.0f\n",
                        i, c.factions[p.faction].id.c_str(), p.pos.x, p.pos.y,
                        p.totalTroops(), p.engaged ? 1 : 0,
                        Vector2Distance(p.pos, gs.player.pos));
        }
        for (int s = 0; s < (int)gs.skirmishes.size(); ++s)
            std::printf("skirmish %d: a=%d b=%d timer=%.1f pos=(%.0f,%.0f)\n",
                        s, gs.skirmishes[s].a, gs.skirmishes[s].b,
                        gs.skirmishes[s].timer, gs.skirmishes[s].pos.x, gs.skirmishes[s].pos.y);
        if (gs.screen == Screen::Battle) {
            const BattleView v = GetBattleView();
            const char* wname = c.weapons.valid(v.heroWeapon)
                                    ? c.weapons[v.heroWeapon].id.c_str() : "none";
            std::printf("battle: heroPos=(%.2f,%.2f,%.2f) yaw=%.3f pitch=%.3f "
                        "hp=%.0f/%.0f weapon=%s allies=%d enemies=%d arrows=%d over=%d won=%d\n",
                        v.heroPos.x, v.heroPos.y, v.heroPos.z, v.heroYaw, v.heroPitch,
                        v.heroHp, v.heroMaxHp, wname, v.aliveAllies, v.aliveEnemies,
                        v.arrowsInFlight, v.over ? 1 : 0, v.won ? 1 : 0);
        }
        if (gs.screen == Screen::Settlement)
            std::printf("settlement=%d (%s)\n", gs.currentSettlement,
                        gs.towns[gs.currentSettlement].name.c_str());
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
        } else if (cmd == "save" || cmd == "load") {
            std::string p;
            if (!(ss >> p)) p = DefaultSavePath();
            const bool ok = (cmd == "save") ? SaveGame(h.gs, p.c_str())
                                            : LoadGame(h.gs, p.c_str());
            std::printf("%s %s: %s\n", cmd.c_str(), p.c_str(), ok ? "ok" : "FAILED");
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
