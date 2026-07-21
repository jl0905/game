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
void DialogueUpdate(GameState& gs, const CampaignInput& in);
void DialogueDraw(const GameState& gs);

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
