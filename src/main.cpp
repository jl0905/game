#include "world.h"
#include "ui.h"
#include "harness.h"
#include "campaign/campaign.h"
#include "battle/battle.h"
#include <cstring>

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

// Capture everything the battle needs from the world map.
BattleSetup MakeBattleSetup(const GameState& gs) {
    BattleSetup s;
    s.playerTroops = gs.player.troopCounts;
    s.enemyTroops  = gs.parties[gs.battlePartyIndex].troopCounts;
    if (gs.battleAllyIndex >= 0 && gs.battleAllyIndex < (int)gs.parties.size())
        s.allyTroops = gs.parties[gs.battleAllyIndex].troopCounts;  // a party joined you
    s.heroLoadout  = gs.playerHero.loadout;
    s.heroMaxHp    = gs.playerHero.maxHp;
    s.campaignPos  = gs.player.pos;   // terrain is generated from where we stand
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    // Headless scripted mode: no window, same simulation.
    if (argc >= 3 && std::strcmp(argv[1], "--script") == 0)
        return RunScript(argv[2]);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "OpenWarband");
    SetTargetFPS(120);
    SetExitKey(KEY_NULL);   // ESC shouldn't insta-quit mid battle
    ui::LoadFonts();        // smooth TTF text everywhere (see assets/fonts.cfg)

    GameState gs;
    LoadDefaultContent(gs.content);   // populate the data-driven catalogue
    CampaignInit(gs);

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
                    SettlementDraw(gs);
                } else {
                    CampaignDraw(gs);
                }
                break;
            }
            case Screen::Settlement: {                    // inside a town; overworld paused
                const CampaignInput in = GatherCampaignInput(gs);
                SettlementUpdate(gs, in);
                if (gs.screen == Screen::Settlement) SettlementDraw(gs);
                else                                 CampaignDraw(gs);
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
        // Quit only when ESC is pressed on the overworld itself — not when it was
        // just consumed to leave a settlement (which lands us back on Campaign).
        if (IsKeyPressed(KEY_ESCAPE) && screenAtFrameStart == Screen::Campaign) break;
    }

    ui::UnloadFonts();
    CloseWindow();
    return 0;
}
