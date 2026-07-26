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
- ACTION NEEDED (user): install the LunarG Vulkan SDK
  (https://vulkan.lunarg.com) — everything else is code.

## Phases (each ships and keeps the game playable)
0. **Seam** — introduce `IRenderer` covering every draw the game makes
   (boxes, capsules-as-boxes, cylinders, terrain mesh, text, 2D). The
   raylib backend implements it 1:1. No visual change; the suite proves
   the sim untouched.
1. **Big-win passes inside raylib** while the seam hardens (these carry
   over as they're scene-level, not API-level): sun shadow mapping,
   SSAO, bloom — stacked on the V151 post pipeline.
2. **Vulkan backend, raster first** — same draws through Vulkan;
   instanced army path ports directly (it is already transform-lists).
   Feature-flag `renderer vulkan|raylib` in settings.cfg; raylib remains
   the fallback until parity.
3. **Vulkan RT hybrid** — ray-traced shadows + AO over the raster frame
   (per-frame TLAS rebuild of soldier boxes is standard practice), then
   RT reflections on water/steel. "Ultra" arrives here.

## Meanwhile: the named pain points get fixed in-engine
- V149 removed the pop-in LOD tiers (one model at all distances).
- V152 rebuilds the terrain (see DIRECTION.md).
