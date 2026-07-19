#include "world.h"
#include "ui.h"
#include "harness.h"
#include "bridge.h"
#include "save.h"
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
int RunBench(int perSide) {
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
    s.campaignPos = { 500, 500 };     // fixed spot -> deterministic terrain
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
    if (argc >= 3 && std::strcmp(argv[1], "--bench") == 0)
        return RunBench(std::atoi(argv[2]));

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "OpenWarband");
    SetTargetFPS(120);
    SetExitKey(KEY_NULL);   // ESC shouldn't insta-quit mid battle
    ui::LoadFonts();        // smooth TTF text everywhere (see assets/fonts.cfg)

    GameState gs;
    LoadDefaultContent(gs.content);   // populate the data-driven catalogue
    CampaignInit(gs);

    float quitArm = 0;   // double-Esc window for quitting
    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        const Screen screenAtFrameStart = gs.screen;
        switch (gs.screen) {
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
                else                              CampaignDraw(gs);
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
    CloseWindow();
    return 0;
}
