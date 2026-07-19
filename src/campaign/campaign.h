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

CampaignInput GatherCampaignInput(const GameState& gs);   // covers settlement too

void CampaignUpdate(GameState& gs, float dt, const CampaignInput& in);
void CampaignDraw(const GameState& gs);

void SettlementUpdate(GameState& gs, const CampaignInput& in);
void SettlementDraw(const GameState& gs);

// Party management screen (roster + veterancy upgrades), opened with P.
void PartyUpdate(GameState& gs, const CampaignInput& in);
void PartyDraw(const GameState& gs);
