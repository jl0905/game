#pragma once
#include "world.h"

// ---------------------------------------------------------------------------
// Save / load. A save is a small line-based text file that stores WORLD STATE
// only — the content catalogue is reloaded, never saved. Everything is keyed
// by content id strings (not handles) so saves survive content additions.
// Battles are not saved; saving is a campaign-screen action.
// ---------------------------------------------------------------------------

// "save.owb" next to the exe (windowed) or the working directory (headless).
const char* DefaultSavePath();

// "autosave.owb" in the same place (written when the game quits).
const char* AutoSavePath();

bool SaveGame(const GameState& gs, const char* path);

// Requires gs.content to be loaded. On success the world is replaced by the
// saved one and the screen is set to Campaign.
bool LoadGame(GameState& gs, const char* path);
