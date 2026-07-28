#pragma once
#include "raylib.h"

// ---------------------------------------------------------------------------
// The one message rail (V189, user call): EVERY transient line the game says
// to the player - campaign notices, button refusals, kill feed, loot pings -
// goes through here and renders in the SAME place on every screen: a fading
// stack anchored bottom-centre, just above the bar. One system, one look.
//
// Push from anywhere (sim or UI); Draw once per frame from each screen's
// draw pass, before rdr::PresentVulkanUi(). Headless: Push is a no-op sink
// (messages still reach the harness via the systems that also log them).
// ---------------------------------------------------------------------------
namespace notify {

// A line with a colour; life in seconds (default ~4s). Newest at the bottom.
void Push(const char* text, Color c = RAYWHITE, float life = 4.0f);

// Kill-feed convenience: "A cut down B" in the killer's team colour.
void Kill(const char* killer, const char* victim, Color teamTint);

void Draw();          // fading bottom-centre stack; call from every screen
void Clear();         // screen transitions that should drop stale lines

}  // namespace notify
