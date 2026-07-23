#pragma once
#include "../world.h"
#include "../input.h"

// ---------------------------------------------------------------------------
// Walkable settlement (roadmap B2). Entering a settlement drops the hero into
// a small 3D scene — procedural buildings seeded from the town, villager NPCs
// wandering the streets, and recruiting at the tavern. Same gather/update/
// draw discipline as everywhere: movement reuses BattleInput (WASD + look),
// menu actions (recruit/leave) come via CampaignInput.
// ---------------------------------------------------------------------------

// Build the scene for gs.towns[gs.currentSettlement]. Call on entry.
void TownInit(const GameState& gs);

// One simulation step. Returns false when the player leaves (caller returns
// to the campaign map).
bool TownUpdate(GameState& gs, float dt, const BattleInput& in, const CampaignInput& cin);

void TownDraw(const GameState& gs);

// True while the hero stands close enough to the tavern to recruit.
bool TownAtTavern();

// Dialogue (direction H4): pressing E beside a villager opens a conversation
// screen. TownTalkNearest fills GameState's dialogue fields from the closest
// NPC (harnesses may call it directly); Dialogue{Update,Draw} run the screen.
void TownTalkNearest(GameState& gs);
void TownTalkLord(GameState& gs);   // the castle court audience (K2)
void TownGoTavern(GameState& gs);   // stand at the tavern door (harness
                                    // shortcut, Q1 — recruiting lives there)

// The settlement gate menu (U4): GUI first, boots by choice. The input
// gatherer asks TownInMenu() so number keys mean rows, not recruits; the
// shared layout keeps hit-boxes and drawing in one place (K7 rule).
bool TownInMenu();
namespace townmenu {
inline constexpr int Y = 168, ROW_H = 34, ROWS = 13, X_HALF = 260;
}
void DialogueUpdate(GameState& gs, const CampaignInput& in);

// Windowed mouse support for the dialogue screen (V27): DialogueDraw records
// where it drew each topic row; this hit-tests them. Returns the menuChoice
// (1..9), DLG_LEAVE for the take-your-leave row, or 0 for no hit.
inline constexpr int DLG_LEAVE = -9;
int DialogueOptionAt(Vector2 mouse);
void DialogueDraw(const GameState& gs);

// Walking-mode service chips are clickable too (V122): TownDraw records the
// rect of every service it prints; this hit-tests them. Returns 0 for no hit
// or one of the SVC_* ids below (SVC_RECRUIT0+n = tavern recruit slot n).
inline constexpr int SVC_TOURNEY = 1, SVC_MARKET = 2, SVC_WORK = 3,
                     SVC_HIRE = 4, SVC_OATH = 5, SVC_TALK = 6,
                     SVC_GARRISON = 7, SVC_RANSOM = 8, SVC_RECRUIT0 = 100;
int TownServiceAt(Vector2 mouse);

// Read-only scene info for harnesses / debugging.
struct TownView {
    Vector3 heroPos{};
    float   heroYaw = 0;
    Vector3 tavernPos{};
    int     npcs = 0;
    bool    atTavern = false;
    bool    inside = false;   // in the tavern common room
};
TownView GetTownView();
