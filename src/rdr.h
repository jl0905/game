#pragma once
#include "raylib.h"
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// The renderer seam (V160, RENDERER.md phase 2 integration). Scene code
// records draw intents into neutral lists; a BACKEND executes them. Backend
// one is raylib (exactly the V126/V128 instanced flush, relocated here);
// backend two is Vulkan (in progress — vkarmy already consumes this exact
// shape: per-instance transform + colour). Scene code that goes through
// rdr:: needs NO further changes when the backend flips.
// ---------------------------------------------------------------------------
namespace rdr {

// One oriented box instance: the universal soldier/limb/prop primitive.
struct BoxInstance {
    Matrix transform;
    // colour lives in the bucket key; kept here for the vk path
};

// The recorded frame: colour-bucketed transforms, exactly what both the GL
// instanced flush and the Vulkan instance buffer want.
using BoxBuckets = std::unordered_map<unsigned, std::vector<Matrix>>;

BoxBuckets& Buckets();               // the current frame's recording
void PushBox(const Matrix& m, Color c);
void PushOrientedBox(Vector3 a, Vector3 b, float r, Color c);   // limb form

// Execute + clear through the active backend. The raylib backend needs the
// instanced cube mesh/material/shader prepared by the caller (battle.cpp
// owns that GL state today); it stays injected so this header stays
// backend-neutral.
struct RaylibInstancedState {
    Mesh*     cube;
    Material* mat;
    Shader    shader;
    int       sunLoc;
    Vector3   sun;
};
void FlushRaylib(const RaylibInstancedState& st);

// The backend switch (V161): reads Settings::renderer. Today `vulkan`
// logs once and executes through GL until the Vulkan executor reaches
// parity (RENDERER.md phase 2 tail: offscreen render + present interop,
// then the native window swap). Scene code never branches — it calls
// Flush() and the seam decides.
void Flush(const RaylibInstancedState& st);

// V172: scene-agnostic entry points to the shared instanced backend (the
// GL state lives in battle.cpp, which owns the instancing shader). Any 3D
// scene can record boxes and flush them through the active backend:
//   rdr::EnsureBackendGL();          // once inside BeginMode3D
//   rdr::PushBox / PushOrientedBox   // record
//   rdr::FlushScene(sunDir);         // execute before EndMode3D
void EnsureBackendGL();
void FlushScene(Vector3 sunDir);

// V162: boots a live Vulkan device inside the game process the first time
// renderer=vulkan flushes a frame; true once the frame executor is usable.
bool VulkanExecutorReady();

// V163: the Vulkan frame executor. Renders `count` 80-byte instances
// (column-major mat4 + rgba floats) offscreen with the game's camera and
// returns RGBA pixels (row 0 = top), or null on failure.
const unsigned char* VulkanRenderFrame(const float* viewProj16, const float* sun4,
                                       const void* instData, int count,
                                       int w, int h);

// V164: stage the battlefield mesh (10 floats/vert: pos3 nrm3 col4) for the
// Vulkan depth-only occluder pass — gives the composited soldiers correct
// hillside occlusion. Safe to call before the device exists.
void VulkanSetTerrain(const float* verts, int vertCount);

// V163: composite the Vulkan-rendered layer over the GL frame. Call once
// per frame after EndMode3D(); no-op unless a Vulkan frame is pending.
void PresentVulkan();

}  // namespace rdr
