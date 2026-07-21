#pragma once
#include "../world.h"
#include "../input.h"

// ---------------------------------------------------------------------------
// Campaign module public interface. Owns the overworld: party movement and AI,
// towns/recruiting, economy, skirmishes, and applying battle outcomes.
//
// Each screen is split three ways so the game can be driven programmatically:
//   Gather*  — read the real devices into an input-intent struct (windowed only)
//   *Update  — pure simulation; touches no raylib input or drawing
//   *Draw    — render the current state (windowed only)
//
// The campaign never runs a battle itself. When a battle should start it sets
// GameState::screen = Screen::Battle (+ battlePartyIndex / battleAllyIndex);
// the caller snapshots the world into a BattleSetup and runs the battle.
// ---------------------------------------------------------------------------

void CampaignInit(GameState& gs);

// Title screen (windowed entry point). TitleUpdate returns false on Quit.
bool TitleUpdate(GameState& gs, const CampaignInput& in);
void TitleDraw(const GameState& gs);

// Victory screen — the campaign is won; any choice returns to the title.
bool VictoryUpdate(GameState& gs, const CampaignInput& in);
void VictoryDraw(const GameState& gs);

CampaignInput GatherCampaignInput(const GameState& gs);   // covers settlement too

void CampaignUpdate(GameState& gs, float dt, const CampaignInput& in);
void CampaignDraw(const GameState& gs);

// (The settlement itself is a walkable 3D scene — see src/town/town.h.)

// Settlement marketplace (buy/sell trade goods), opened with M in a settlement.
void MarketUpdate(GameState& gs, const CampaignInput& in);
void MarketDraw(const GameState& gs);

// Party management screen (roster + veterancy upgrades), opened with P.
void PartyUpdate(GameState& gs, const CampaignInput& in);
void PartyDraw(const GameState& gs);

// Tiled inventory screen (grid loot + equipping), opened with I.
void InventoryUpdate(GameState& gs, const CampaignInput& in);
void InventoryDraw(const GameState& gs);

// Character sheet (level / XP / attributes), opened with C.
void CharacterUpdate(GameState& gs, const CampaignInput& in);
void CharacterDraw(const GameState& gs);
