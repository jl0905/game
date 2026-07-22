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

// Save slots (N3): three named quicksave files beside the autosave, and a
// cheap header peek (day/gold) for the load menu's listing.
const char* SaveSlotPath(int slot);              // 1..3
bool PeekSave(const char* path, int& day, int& gold);

// The saga (V100): write the chronicle + reign stats to saga.txt beside the
// saves. Called once per ending (victory or the warband's fall).
void WriteSaga(const GameState& gs, const char* ending);

bool SaveGame(const GameState& gs, const char* path);

// Requires gs.content to be loaded. On success the world is replaced by the
// saved one and the screen is set to Campaign.
bool LoadGame(GameState& gs, const char* path);
