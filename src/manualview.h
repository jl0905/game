#pragma once
#include "world.h"

// ---------------------------------------------------------------------------
// The in-game manual (V185): a paginated, mouse-first reference for every
// player option, loaded at runtime from assets/manual.txt so docs ship as
// data. F1 opens it from the map and the paused menu screens; Esc/F1
// returns to wherever the player was.
//
// manual.txt format (line-based):
//   "# Title"    starts a new page
//   "## Title"   a section heading inside the page
//   ""           spacing
//   anything     body text (word-wrapped to the window at draw time)
// ---------------------------------------------------------------------------

void ManualViewOpen(GameState& gs);          // remembers the current screen
void ManualViewUpdate(GameState& gs);        // nav + close (windowed only)
void ManualViewDraw(const GameState& gs);    // full-screen page render
