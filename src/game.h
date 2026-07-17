#pragma once
#include "world.h"

// ---------------------------------------------------------------------------
// Subsystem entry points. main.cpp drives the screen state machine; each
// subsystem owns one screen and both updates and draws within a frame.
// ---------------------------------------------------------------------------

// campaign.cpp — overworld map, party AI, towns/recruiting, economy.
void CampaignInit(GameState& gs);
void CampaignUpdateDraw(GameState& gs, float dt);

// battle.cpp — real-time 3D battle, player combat, soldier AI, casualties.
void BattleInit(GameState& gs);
void BattleUpdateDraw(GameState& gs, float dt);
