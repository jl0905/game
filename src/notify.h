#pragma once
#include "raylib.h"

// ---------------------------------------------------------------------------
// THE game log (V189 rail, unified top-left V204 by user call): EVERY
// transient line the game says to the player - campaign news, party alerts,
// battle kills, loot pings, button refusals - goes through here and renders
// in the SAME place on every screen: a fading stack anchored TOP-LEFT, below
// the status block, newest line first. Entries differ only by colour and
// font size; there are no other notification surfaces.
//
// Push from anywhere (sim or UI); Draw once per frame from each screen's
// draw pass, before rdr::PresentVulkanUi(). The log is global state - it
// SURVIVES screen transitions, so a kill feed from the battle is still
// fading out on the campaign map. Headless: Push is a no-op sink (messages
// still reach the harness via the systems that also log them).
// ---------------------------------------------------------------------------
namespace notify {

// A line with a colour, life in seconds, and a font size. Newest on top.
void Push(const char* text, Color c = RAYWHITE, float life = 6.0f,
          int size = 18);

// Kill-feed convenience: "A cut down B" in the killer's team colour.
void Kill(const char* killer, const char* victim, Color teamTint);

void Draw();          // fading top-left stack; call from every screen
void Clear();         // for flows that genuinely must drop stale lines

}  // namespace notify
