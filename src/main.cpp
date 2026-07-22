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
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
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

// ---- render benchmark (--bench N): N-vs-N synthetic battle, uncapped FPS ----
int RunBench(int perSide, Vector2 where) {
    if (perSide <= 0) perSide = 300;
    SetConfigFlags(FLAG_MSAA_4X_HINT);          // no vsync: measure real speed
    InitWindow(1280, 720, "OpenWarband bench");
    SetTargetFPS(0);

    GameState gs;
    LoadDefaultContent(gs.content);

    BattleSetup s;
    s.playerTroops.assign(gs.content.troops.size(), 0);
    s.enemyTroops.assign(gs.content.troops.size(), 0);
    // Mixed composition; exact mix is irrelevant to rendering cost.
    for (int t = 0; t < gs.content.troops.size(); ++t) {
        s.playerTroops[t] = perSide / gs.content.troops.size();
        s.enemyTroops[t]  = perSide / gs.content.troops.size();
    }
    Character hero;   // default loadout-less hero; battle handles it
    s.heroLoadout = hero.loadout;
    s.heroMaxHp   = 1000000;          // don't die mid-benchmark
    s.campaignPos = where;            // fixed spot -> deterministic terrain
    BattleInit(gs.content, s);

    const int WARMUP = 60, FRAMES = 600;
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
    CloseWindow();

    std::sort(ms.begin(), ms.end());
    const float avg = std::accumulate(ms.begin(), ms.end(), 0.0f) / (float)ms.size();
    const float p99 = ms[(size_t)((float)ms.size() * 0.99f)];
    FILE* f = std::fopen("bench.txt", "w");
    if (f) {
        std::fprintf(f, "soldiers=%d frames=%d avg_ms=%.2f p99_ms=%.2f avg_fps=%.0f\n",
                     perSide * 2, (int)ms.size(), avg, p99, 1000.0f / avg);
        std::fclose(f);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    // Headless scripted mode: no window, same simulation.
    if (argc >= 3 && std::strcmp(argv[1], "--script") == 0)
        return RunScript(argv[2]);
    // Render benchmark: N-vs-N battle, uncapped FPS, results to bench.txt.
    // Optional trailing "x y" picks the battlefield spot (terrain/weather).
    if (argc >= 3 && std::strcmp(argv[1], "--bench") == 0) {
        Vector2 where = { 500, 500 };
        if (argc >= 5) where = { (float)std::atof(argv[3]), (float)std::atof(argv[4]) };
        return RunBench(std::atoi(argv[2]), where);
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    LoadSettings();   // assets/settings.cfg: window, LOD, audio, input comfort
    const Settings& st = GetSettings();
    InitWindow(st.windowWidth, st.windowHeight, "OpenWarband");
    if (st.fullscreen) ToggleFullscreen();
    SetTargetFPS(120);
    SetExitKey(KEY_NULL);   // ESC shouldn't insta-quit mid battle
    ui::LoadFonts();        // smooth TTF text everywhere (see assets/fonts.cfg)
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
            case Screen::Battle: {
                const BattleInput in = GatherBattleInput();
                BattleOutcome out;
                if (BattleUpdate(gs.content, dt, in, out)) {
                    BattleDraw(gs.content);
                } else {
                    gs.battleWon    = out.won;            // hand the result to the world
                    gs.playerLosses = out.playerLosses;
                    gs.allyLosses   = out.allyLosses;
                    gs.enemyLosses  = out.enemyLosses;
                    gs.battleHorses = out.horsesTaken;
                    gs.screen = Screen::BattleResult;
                    CampaignUpdate(gs, dt, CampaignInput{});  // apply + draw the map
                    CampaignDraw(gs);
                }
                break;
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
