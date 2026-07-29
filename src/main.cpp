#include "world.h"
#include "ui.h"
#include "harness.h"
#include "bridge.h"
#include "save.h"
#include "settings.h"
#include "sfx.h"
#include "campaign/campaign.h"
#include "battle/battle.h"
#include "town/town.h"
#include "manualview.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// main.cpp is the only place campaign and battle meet. Each frame:
// gather real input → update the active screen's simulation → draw it.
// The campaign requests a battle by setting Screen::Battle; we snapshot the
// world into a BattleSetup, run the battle to completion, and write its
// BattleOutcome back into the world for the campaign to apply.
//
// `openwarband --script file` runs the same simulation headless, driven by a
// command script instead of the devices (see src/harness.cpp).
// ---------------------------------------------------------------------------

namespace {

// ---- render benchmark (V186 modernization) --------------------------------
// `--bench N [x y]` measures one N-vs-N battle; `--bench sweep` measures the
// standard ladder (300/600/1000/2000 a side) in one run. The whole modern
// stack is exercised and REPORTED: renderer backend, body style, shadows,
// post fx, armour skins, full soldier AI + physics. The hero wears the top
// troop's real loadout so weapon/armour paths are hot, and each line in
// bench.txt now carries the config it was measured under (fields appended -
// existing avg_ms/p99 parsers keep working).
struct BenchRow {
    int soldiers, frames;
    float avg, p50, p99;
};

BenchRow BenchOne(int perSide, Vector2 where) {
    GameState gs;
    LoadDefaultContent(gs.content);
    GetSettings().battleSize = 1e6f;   // the bench measures ALL of them (V75)

    BattleSetup s;
    s.playerTroops.assign(gs.content.troops.size(), 0);
    s.enemyTroops.assign(gs.content.troops.size(), 0);
    // Mixed composition across every troop type - infantry, archers and
    // cavalry all present so AI, arrows and horses are all on the clock.
    for (int t = 0; t < gs.content.troops.size(); ++t) {
        s.playerTroops[t] = perSide / gs.content.troops.size();
        s.enemyTroops[t]  = perSide / gs.content.troops.size();
    }
    // The hero fights in the top troop's REAL kit (armour skin tiers, arsenal
    // swaps and the whole loadout render path get measured, not skipped).
    Character hero;
    if (gs.content.troops.size() > 0)
        hero.loadout = gs.content.troops[gs.content.troops.size() - 1].loadout;
    s.heroLoadout = hero.loadout;
    s.heroMaxHp   = 1000000;          // don't die mid-benchmark
    s.campaignPos = where;            // fixed spot -> deterministic terrain
    BattleInit(gs.content, s);

    const int WARMUP = 90, FRAMES = 600;
    std::vector<float> ms;
    ms.reserve(FRAMES);
    for (int i = 0; i < WARMUP + FRAMES && !WindowShouldClose(); ++i) {
        const float dt = 1.0f / 60.0f;
        BattleOutcome out;
        const double t0 = GetTime();
        BattleUpdate(gs.content, dt, BattleInput{}, out);
        BattleDraw(gs.content);
        if (i >= WARMUP) ms.push_back((float)((GetTime() - t0) * 1000.0));
    }

    std::sort(ms.begin(), ms.end());
    BenchRow r;
    r.soldiers = perSide * 2;
    r.frames   = (int)ms.size();
    r.avg = std::accumulate(ms.begin(), ms.end(), 0.0f) / (float)ms.size();
    r.p50 = ms[ms.size() / 2];
    r.p99 = ms[(size_t)((float)ms.size() * 0.99f)];
    return r;
}

int RunBench(int perSide, Vector2 where, bool sweep) {
    SetConfigFlags(FLAG_MSAA_4X_HINT);          // no vsync: measure real speed
    InitWindow(1280, 720, "OpenWarband bench");
    SetTargetFPS(0);

    const Settings& st = GetSettings();
    const std::string cfg = TextFormat(
        "renderer=%s bodystyle=%s shadows=%s postfx=%s",
        st.renderer == 1 ? "vulkan" : "raylib",
        st.bodyStyle == 2 ? "pill" : st.bodyStyle == 1 ? "blocky" : "boxy",
        st.shadows ? "on" : "off", st.postFx ? "on" : "off");

    FILE* f = std::fopen("bench.txt", "w");
    const int ladder[] = { 300, 600, 1000, 2000 };
    for (const int n : sweep ? std::vector<int>(ladder, ladder + 4)
                             : std::vector<int>{ perSide > 0 ? perSide : 300 }) {
        const BenchRow r = BenchOne(n, where);
        if (f) {
            std::fprintf(f,
                         "soldiers=%d frames=%d avg_ms=%.2f p99_ms=%.2f "
                         "avg_fps=%.0f p50_ms=%.2f %s\n",
                         r.soldiers, r.frames, r.avg, r.p99, 1000.0f / r.avg,
                         r.p50, cfg.c_str());
            std::fflush(f);
        }
        if (WindowShouldClose()) break;
    }
    if (f) std::fclose(f);
    CloseWindow();
    return 0;
}

// ---- frame capture (V190 verification) ------------------------------------
// `--shots N dir [x y]` runs the same N-vs-N battle the bench does, windowed,
// and exports screenshot PAIRS of consecutive frames at a fixed cadence into
// `dir` (shot_F.png, shot_F+1.png). Consecutive pairs are what make temporal
// artifacts (z-fighting shimmer) visible offline: a stable seam renders
// identically twice, a fighting one flickers between the frames. The hero
// raises the guard for the tail third so the block pose is captured too.
int RunShots(int perSide, const char* dir, Vector2 where) {
    InitWindow(1280, 720, "OpenWarband shots");
    SetTargetFPS(0);

    GameState gs;
    LoadDefaultContent(gs.content);
    GetSettings().battleSize = 1e6f;

    BattleSetup s;
    s.playerTroops.assign(gs.content.troops.size(), 0);
    s.enemyTroops.assign(gs.content.troops.size(), 0);
    for (int t = 0; t < gs.content.troops.size(); ++t) {
        s.playerTroops[t] = perSide / gs.content.troops.size();
        s.enemyTroops[t]  = perSide / gs.content.troops.size();
    }
    Character hero;
    if (gs.content.troops.size() > 0)
        hero.loadout = gs.content.troops[gs.content.troops.size() - 1].loadout;
    s.heroLoadout = hero.loadout;
    s.heroMaxHp   = 1000000;
    s.campaignPos = where;
    BattleInit(gs.content, s);

    const int TOTAL = 900, EVERY = 60;
    for (int i = 0; i < TOTAL && !WindowShouldClose(); ++i) {
        BattleInput in{};
        in.block = i >= TOTAL * 2 / 3;   // capture the raised-guard pose too
        BattleOutcome out;
        BattleUpdate(gs.content, 1.0f / 60.0f, in, out);
        BattleDraw(gs.content);
        if (i % EVERY == 0 || i % EVERY == 1) {   // consecutive pair
            Image shot = LoadImageFromScreen();
            ExportImage(shot, TextFormat("%s/shot_%04d.png", dir, i));
            UnloadImage(shot);
        }
    }
    CloseWindow();
    return 0;
}

// V193: `--shots-trip dir` captures the campaign -> battle -> campaign round
// trip that broke when the V192 sky bracket re-enabled UI recording: frame
// captures of the map BEFORE a battle and AFTER returning from one must look
// the same (the map is world-space ui:: and must never land in the
// screen-space Vulkan overlay).
int RunTrip(const char* dir) {
    InitWindow(1280, 720, "OpenWarband trip");
    SetTargetFPS(0);
    ui::LoadFonts();
    ui::SetTextScale(GetSettings().textScale);

    GameState gs;
    LoadDefaultContent(gs.content);
    CampaignInit(gs);
    gs.screen = Screen::Campaign;

    auto shoot = [&](const char* name) {
        Image shot = LoadImageFromScreen();
        ExportImage(shot, TextFormat("%s/%s.png", dir, name));
        UnloadImage(shot);
    };
    for (int i = 0; i < 5; ++i) CampaignDraw(gs);
    shoot("map_before");

    BattleSetup s;   // the bench composition; the sim never touches gs
    s.playerTroops.assign(gs.content.troops.size(), 2);
    s.enemyTroops.assign(gs.content.troops.size(), 2);
    s.heroLoadout = gs.content.troops.size() > 0
        ? gs.content.troops[gs.content.troops.size() - 1].loadout : Loadout{};
    s.heroMaxHp = 1000000;
    s.campaignPos = { 500, 500 };
    BattleInit(gs.content, s);
    for (int i = 0; i < 90; ++i) {
        BattleOutcome out;
        BattleUpdate(gs.content, 1.0f / 60.0f, BattleInput{}, out);
        BattleDraw(gs.content);
    }
    shoot("battle");

    for (int i = 0; i < 5; ++i) CampaignDraw(gs);
    shoot("map_after");
    CloseWindow();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    // Headless scripted mode: no window, same simulation.
    if (argc >= 3 && std::strcmp(argv[1], "--script") == 0)
        return RunScript(argv[2]);
    // Round-trip capture (V193): campaign -> battle -> campaign frames.
    if (argc >= 3 && std::strcmp(argv[1], "--shots-trip") == 0) {
        LoadSettings();
        return RunTrip(argv[2]);
    }
    // Frame capture for visual verification (V190): see RunShots above.
    if (argc >= 4 && std::strcmp(argv[1], "--shots") == 0) {
        Vector2 where = { 500, 500 };
        if (argc >= 6) where = { (float)std::atof(argv[4]), (float)std::atof(argv[5]) };
        LoadSettings();
        return RunShots(std::atoi(argv[2]), argv[3], where);
    }
    // Render benchmark: N-vs-N battle, uncapped FPS, results to bench.txt.
    // Optional trailing "x y" picks the battlefield spot (terrain/weather).
    if (argc >= 3 && std::strcmp(argv[1], "--bench") == 0) {
        Vector2 where = { 500, 500 };
        if (argc >= 5) where = { (float)std::atof(argv[3]), (float)std::atof(argv[4]) };
        LoadSettings();   // honour renderer/shadows/postfx in the measurement
        const bool sweep = std::strcmp(argv[2], "sweep") == 0;   // V186 ladder
        return RunBench(sweep ? 0 : std::atoi(argv[2]), where, sweep);
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    LoadSettings();   // assets/settings.cfg: window, LOD, audio, input comfort
    const Settings& st = GetSettings();
    InitWindow(st.windowWidth, st.windowHeight, "OpenWarband");
    if (st.fullscreen) ToggleFullscreen();
    SetTargetFPS(120);
    SetExitKey(KEY_NULL);   // ESC shouldn't insta-quit mid battle
    ui::LoadFonts();        // smooth TTF text everywhere (see assets/fonts.cfg)
    ui::SetTextScale(GetSettings().textScale);   // the player's letters win (V72)
    SfxInit();              // procedural sound effects
    SetMasterVolume(st.masterVolume);

    GameState gs;
    LoadDefaultContent(gs.content);   // populate the data-driven catalogue
    CampaignInit(gs);
    gs.screen = Screen::Title;        // windowed play begins at the title

    float quitArm = 0;   // double-Esc window for quitting
    bool  running = true;
    while (running && !WindowShouldClose()) {
        const float dt = GetFrameTime();
        const Screen screenAtFrameStart = gs.screen;
        switch (gs.screen) {
            case Screen::Title: {
                const CampaignInput in = GatherCampaignInput(gs);
                if (!TitleUpdate(gs, in)) { running = false; break; }
                if (gs.screen == Screen::Title) TitleDraw(gs);
                else                            CampaignDraw(gs);
                break;
            }
            case Screen::Campaign:
            case Screen::BattleResult: {
                const CampaignInput in = GatherCampaignInput(gs);
                CampaignUpdate(gs, dt, in);
                if (in.openManual && gs.screen == Screen::Campaign) {
                    ManualViewOpen(gs);       // the bottom-bar chip (V188)
                    ManualViewDraw(gs);
                    break;
                }
                if (gs.screen == Screen::Battle) {        // campaign requested a battle
                    BattleInit(gs.content, MakeBattleSetup(gs));
                    BattleDraw(gs.content);
                } else if (gs.screen == Screen::Settlement) {
                    TownInit(gs);                          // step into the streets
                    TownDraw(gs);
                } else {
                    CampaignDraw(gs);
                }
                break;
            }
            case Screen::Settlement: {                    // walking the streets; paused
                const CampaignInput cin = GatherCampaignInput(gs);
                const BattleInput   bin = GatherBattleInput();
                if (TownUpdate(gs, dt, bin, cin)) TownDraw(gs);
                else if (gs.screen == Screen::Battle) {   // stepped into the ring
                    BattleInit(gs.content, MakeBattleSetup(gs));
                    BattleDraw(gs.content);
                } else                            CampaignDraw(gs);
                break;
            }
            case Screen::Kingdom: {                       // the ledger (O1)
                const CampaignInput in = GatherCampaignInput(gs);
                KingdomUpdate(gs, in);
                if (gs.screen == Screen::Kingdom) KingdomDraw(gs);
                else                              CampaignDraw(gs);
                break;
            }
            case Screen::Parley: {                        // words before steel (V136)
                const CampaignInput in = GatherCampaignInput(gs);
                ParleyUpdate(gs, in);
                if (gs.screen == Screen::Battle) {
                    BattleInit(gs.content, MakeBattleSetup(gs));
                    BattleDraw(gs.content);
                } else if (gs.screen == Screen::Parley) ParleyDraw(gs);
                else                                    CampaignDraw(gs);
                break;
            }
            case Screen::Estate: {                        // the manor hall (V135)
                const CampaignInput in = GatherCampaignInput(gs);
                EstateUpdate(gs, in);
                if (gs.screen == Screen::Estate) EstateDraw(gs);
                else                             CampaignDraw(gs);
                break;
            }
            case Screen::Quests: {                        // the journal (V124)
                const CampaignInput in = GatherCampaignInput(gs);
                QuestsUpdate(gs, in);
                if (gs.screen == Screen::Quests) QuestsDraw(gs);
                else                             CampaignDraw(gs);
                break;
            }
            case Screen::LoadMenu: {                      // pick a save (N3)
                const CampaignInput in = GatherCampaignInput(gs);
                LoadMenuUpdate(gs, in);
                if (gs.screen == Screen::LoadMenu)      LoadMenuDraw(gs);
                else if (gs.screen == Screen::Title)    TitleDraw(gs);
                else                                    CampaignDraw(gs);
                break;
            }
            case Screen::Background: {                    // who were you? (N2)
                const CampaignInput in = GatherCampaignInput(gs);
                BackgroundUpdate(gs, in);
                if (gs.screen == Screen::Background) BackgroundDraw(gs);
                else                                 CampaignDraw(gs);
                break;
            }
            case Screen::Settings: {                      // options; paused
                const CampaignInput in = GatherCampaignInput(gs);
                SettingsUpdate(gs, in);
                if (gs.screen == Screen::Settings)      SettingsDraw(gs);
                else if (gs.screen == Screen::Title)    TitleDraw(gs);
                else                                    CampaignDraw(gs);
                break;
            }
            case Screen::Dialogue: {                      // a word with a local; paused
                const CampaignInput in = GatherCampaignInput(gs);
                DialogueUpdate(gs, in);
                if (gs.screen == Screen::Dialogue) DialogueDraw(gs);
                else                               TownDraw(gs);
                break;
            }
            case Screen::Market: {                        // buy/sell goods; paused
                const CampaignInput in = GatherCampaignInput(gs);
                MarketUpdate(gs, in);
                if (gs.screen == Screen::Market) MarketDraw(gs);
                else if (gs.screen == Screen::Settlement) { TownInit(gs); TownDraw(gs); }
                else                                      CampaignDraw(gs);
                break;
            }
            case Screen::Party: {                         // roster + upgrades; paused
                const CampaignInput in = GatherCampaignInput(gs);
                PartyUpdate(gs, in);
                if (gs.screen == Screen::Party) PartyDraw(gs);
                else                            CampaignDraw(gs);
                break;
            }
            case Screen::Inventory: {                     // tiled bag; paused
                const CampaignInput in = GatherCampaignInput(gs);
                InventoryUpdate(gs, in);
                if (gs.screen == Screen::Inventory) InventoryDraw(gs);
                else                                CampaignDraw(gs);
                break;
            }
            case Screen::Character: {                     // hero sheet; paused
                const CampaignInput in = GatherCampaignInput(gs);
                CharacterUpdate(gs, in);
                if (gs.screen == Screen::Character) CharacterDraw(gs);
                else                                CampaignDraw(gs);
                break;
            }
            case Screen::Victory: {                       // the campaign is won
                const CampaignInput in = GatherCampaignInput(gs);
                VictoryUpdate(gs, in);
                if (gs.screen == Screen::Victory) VictoryDraw(gs);
                else                              TitleDraw(gs);
                break;
            }
            case Screen::ManualView: {           // the in-game manual (V185)
                ManualViewUpdate(gs);
                ManualViewDraw(gs);              // one last frame on close is fine
                break;
            }
            case Screen::Battle: {
                const BattleInput in = GatherBattleInput();
                BattleOutcome out;
                if (BattleUpdate(gs.content, dt, in, out)) {
                    BattleDraw(gs.content);
                } else {
                    gs.battleWon    = out.won;            // hand the result to the world
                    gs.playerLosses = out.playerLosses;
                    gs.allyLosses   = out.allyLosses;
                    gs.enemyLosses   = out.enemyLosses;
                    gs.battleHorses  = out.horsesTaken;
                    gs.battleYielded  = out.enemySurrendered;
                    gs.battleSlewLord = out.slewLord;
                    gs.screen = Screen::BattleResult;
                    CampaignUpdate(gs, dt, CampaignInput{});  // apply + draw the map
                    CampaignDraw(gs);
                }
                break;
            }
        }
        // F1 opens the manual from the map and any paused menu screen (V185).
        // Battle keeps F1 for the HOLD order; the manual waits for the camp.
        if (IsKeyPressed(KEY_F1)) {
            switch (screenAtFrameStart) {
                case Screen::Campaign: case Screen::Settlement:
                case Screen::Market:   case Screen::Party:
                case Screen::Inventory: case Screen::Character:
                case Screen::Kingdom:  case Screen::Quests:
                case Screen::Estate:   case Screen::Settings:
                case Screen::Title:
                    if (gs.screen == screenAtFrameStart) ManualViewOpen(gs);
                    break;
                default: break;
            }
        }
        // Quit needs a deliberate double-Esc on the overworld itself — not one
        // consumed leaving a sub-screen — and always writes an autosave.
        quitArm -= dt;
        if (IsKeyPressed(KEY_ESCAPE) && screenAtFrameStart == Screen::Campaign) {
            if (quitArm > 0) {
                SaveGame(gs, AutoSavePath());
                break;
            }
            quitArm = 2.0f;
            gs.resultText = "Press Esc again to quit (autosaves).";
        }
    }

    ui::UnloadFonts();
    SfxShutdown();
    CloseWindow();
    return 0;
}
