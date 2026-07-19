#pragma once

// ---------------------------------------------------------------------------
// Procedural sound effects — synthesized at startup, no asset files.
// All calls are safe headless (no audio device = silent no-ops).
// ---------------------------------------------------------------------------

enum class Sfx { Thud, Clang, Loose, Swing, Gallop, Click, Fanfare, Knell };

void SfxInit();       // call once after InitWindow (opens the audio device)
void SfxShutdown();
void SfxPlay(Sfx s, float volume = 1.0f);   // rate-limited per effect
