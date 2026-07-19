#pragma once

// ---------------------------------------------------------------------------
// Procedural sound effects — synthesized at startup, no asset files.
// All calls are safe headless (no audio device = silent no-ops).
// ---------------------------------------------------------------------------

enum class Sfx { Thud, Clang, Loose, Swing, Gallop, Click, Fanfare, Knell, WarCry };

void SfxInit();       // call once after InitWindow (opens the audio device)
void SfxShutdown();
void SfxPlay(Sfx s, float volume = 1.0f);   // rate-limited per effect

// Battle ambience: call every frame while a battle draws; keeps a wind/din
// loop running at `volume` (0 stops it decaying naturally). Headless no-op.
void SfxAmbience(float volume);
