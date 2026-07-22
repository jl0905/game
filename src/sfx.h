#pragma once

// ---------------------------------------------------------------------------
// Procedural sound effects — synthesized at startup, no asset files.
// All calls are safe headless (no audio device = silent no-ops).
// ---------------------------------------------------------------------------

enum class Sfx { Thud, Clang, Loose, Swing, Gallop, Click, Fanfare, Knell, WarCry };

void SfxInit();       // call once after InitWindow (opens the audio device)
void SfxShutdown();
void SfxPlay(Sfx s, float volume = 1.0f);   // rate-limited per effect

// Looping beds (N5): a plucked lute for taverns, a low modal drone for the
// campaign map. Volume 0 lets them fall silent; both are synthesized.
void SfxMinstrel(float volume);
void SfxMusic(float volume);

// Battle ambience: call every frame while a battle draws; keeps a wind/din
// loop running at `volume` (0 stops it decaying naturally). Headless no-op.
void SfxAmbience(float volume);

// Rain bed: a steady patter loop for battles fought in the rain. Call every
// frame it should sound (volume 0 lets it die out). Headless no-op.
void SfxRain(float volume);

// War drums (V114): a deep skin pattern looped under battles, volume
// swelling with the fight. Headless no-op.
void SfxDrums(float volume);
