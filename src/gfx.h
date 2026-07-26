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

// Sun shadow mapping (V153, renderer-migration phase 1). A 2048 depth-only
// target rendered from the sun each frame; the lit + instanced shaders
// sample it with PCF. Flow per frame (inside BeginDrawing, BEFORE PostBegin):
//   Matrix lightVP = ShadowBegin(sunDir, center);   // ortho depth pass on
//   ...draw casters (terrain model, soldier boxes)...
//   ShadowEnd();
//   ShadowBind(shader, lightVpLoc, mapLoc);         // per receiving shader
// Headless or `shadows off` in settings.cfg: ShadowBegin returns identity
// and the passes are no-ops.
Matrix ShadowBegin(Vector3 sunDir, Vector3 center);
void   ShadowEnd();
void   ShadowBind(Shader sh, int lightVpLoc, int mapLoc, Matrix lightVP);
bool   ShadowsOn();
