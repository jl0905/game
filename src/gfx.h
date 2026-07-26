#pragma once
#include "raylib.h"

// ---------------------------------------------------------------------------
// Shared graphics helpers (windowed only).
// ---------------------------------------------------------------------------

// Lazily-loaded directional diffuse shader (ambient floor + one sun light).
// Apply with BeginShaderMode for rlgl shapes, or set it as a model material
// shader. Safe headless: returns an empty Shader when no window exists.
Shader GetLitShader();

// Cinematic post pipeline (V151, user "ultra graphics" ask): render the 3D
// scene into an offscreen target, then present it through one filmic pass —
// ACES tone mapping, a warm/teal grade, vignette, and living film grain.
// PostBegin() starts capture (no-op headless or with Post FX off in
// settings — then it's a plain passthrough and costs nothing); PostEnd()
// draws the graded frame. HUD drawn after PostEnd() stays crisp.
void PostBegin();
void PostEnd();
