#pragma once
#include "raylib.h"

// ---------------------------------------------------------------------------
// Shared graphics helpers (windowed only).
// ---------------------------------------------------------------------------

// Lazily-loaded directional diffuse shader (ambient floor + one sun light).
// Apply with BeginShaderMode for rlgl shapes, or set it as a model material
// shader. Safe headless: returns an empty Shader when no window exists.
Shader GetLitShader();
