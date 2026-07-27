# The Renderer Migration (started V152, user mandate 2026-07-26)

Goal: leave raylib's GL 3.3 ceiling for a modern renderer with real
lighting — shadow-mapped sun now, hardware ray tracing (Vulkan RT) as the
endgame. Cost is explicitly no object (user directive); CORRECTNESS is:
the simulation and its 137-script harness must never break. The sim never
touches rendering (Gather/Update/Draw discipline), which makes this a
renderer-only transplant.

## Toolchain reality (checked 2026-07-26)
- No MSVC on this machine → DirectX Raytracing (DXR) is out for now.
- MinGW + Vulkan SDK works: **Vulkan is the target API**, ray tracing via
  VK_KHR_ray_tracing_pipeline (NVIDIA/AMD/Intel all ship it).
- ~~ACTION NEEDED: install the Vulkan SDK~~ **CLEARED (V155), no SDK
  needed at all**: Khronos headers vendored at third_party/vulkan-headers,
  the driver-shipped loader (vulkan-1.dll) loads dynamically (no import
  lib, MinGW-clean), and tools/vkprobe.c PROVES the lane: both GPUs
  (RTX 4060 Laptop, Vulkan 1.4; Radeon 610M, 1.3) report
  ray_tracing_pipeline + acceleration_structure + ray_query = YES.
  Shader compilation: vendor a glslang release zip (no installer) or
  precompile SPIR-V — decided at phase 2 start.

## Phases (each ships and keeps the game playable)
0. **Seam** — introduce `IRenderer` covering every draw the game makes
   (boxes, capsules-as-boxes, cylinders, terrain mesh, text, 2D). The
   raylib backend implements it 1:1. No visual change; the suite proves
   the sim untouched.
1. **Big-win passes inside raylib** while the seam hardens — SUN SHADOW
   MAPPING SHIPPED (V153): 2048 depth target rendered from the sun each
   battle frame, PCF-sampled by the terrain shader, hard-sampled by the
   instanced army shader; soldiers cast as boxes, trees as canopies;
   `shadows on|off` in settings.cfg. SSAO and bloom remain (these carry
   over as they're scene-level, not API-level): sun shadow mapping,
   SSAO, bloom — stacked on the V151 post pipeline.
2. **Vulkan backend, raster first** — MILESTONE 1 SHIPPED (V156):
   tools/vkdemo.c opens a Win32 window, builds a real swapchain and
   PRESENTS vsynced frames on the RTX 4060 from MinGW with no SDK
   ("vkdemo: PRESENTED 180 frames on NVIDIA GeForce RTX 4060 Laptop
   GPU"). Next milestones: SPIR-V pipeline (vendor glslang or embed
   precompiled shaders) → triangle → instanced boxes → the army. — same draws through Vulkan;
   instanced army path ports directly (it is already transform-lists).
   Feature-flag `renderer vulkan|raylib` in settings.cfg; raylib remains
   the fallback until parity.
3. **Vulkan RT hybrid** — ray-traced shadows + AO over the raster frame
   (per-frame TLAS rebuild of soldier boxes is standard practice), then
   RT reflections on water/steel. "Ultra" arrives here.

## Meanwhile: the named pain points get fixed in-engine
- V149 removed the pop-in LOD tiers (one model at all distances).
- V152 rebuilds the terrain (see DIRECTION.md).
