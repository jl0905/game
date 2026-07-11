#include "game.h"

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "OpenWarband");
    SetTargetFPS(120);
    SetExitKey(KEY_NULL);   // ESC shouldn't insta-quit mid battle

    GameState gs;
    CampaignInit(gs);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        switch (gs.screen) {
            case Screen::Campaign:
            case Screen::BattleResult:
                CampaignUpdateDraw(gs, dt);
                break;
            case Screen::Battle:
                BattleUpdateDraw(gs, dt);
                break;
        }
        if (IsKeyPressed(KEY_ESCAPE) && gs.screen == Screen::Campaign) break;
    }

    CloseWindow();
    return 0;
}
