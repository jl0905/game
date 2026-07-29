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
   GPU"). MILESTONES 2+3 SHIPPED (V157): glslang vendored
   (third_party/glslang-bin, no installer), GLSL→SPIR-V shaders in
   assets/spv, and tools/vkarmy.c runs a full graphics pipeline —
   depth buffer, per-instance transforms+colour, push-constant camera,
   Lambert sun — drawing 2000 marching soldiers + ground in ONE draw:
   "avg 0.62 ms (1618 fps) on NVIDIA GeForce RTX 4060 Laptop GPU".
   MILESTONE 4 (V158): the battlefield composition —
   heightfield terrain mesh (the game's smoothstep-hill construction,
   smooth normals, grass/dirt/rock palette) + 80 trees + the army, in
   TWO draw calls: 0.75 ms / 1340 fps. MILESTONE 5 (V159): text + 2D on
   Vulkan — a GDI-rasterised Consolas atlas (no font files), a
   descriptor-sampled alpha-blended overlay pipeline, live HUD text +
   panels over the battle: 0.71 ms / 1415 fps, three draw calls total.
   EVERY rendering unknown is now retired: present, SPIR-V pipelines,
   instancing, terrain meshes, descriptors/texturing, blending, text.
   THE SEAM IS IN THE GAME (V160): src/rdr.h|.cpp
   is the neutral recording layer — colour-bucketed box transforms,
   the exact instance shape vkarmy consumes — and the ENTIRE battle
   instanced path (every batched soldier, limb, corpse and prop) now
   records through rdr::PushBox/PushOrientedBox and executes through
   rdr::FlushRaylib. Zero behaviour change: bench 16.23ms, 137 green.
   Remaining wiring: (a2) the Vulkan executor for the same recording
   (window/input swap is the tail); (b) swapchain resize;
   (c) the campaign/town call-site conversion screen by screen;
   (d) `renderer vulkan` in settings.cfg flips the backend, raylib
   stays as fallback until parity, then retires. — same draws through Vulkan;
   instanced army path ports directly (it is already transform-lists).
   Feature-flag `renderer vulkan|raylib` in settings.cfg; raylib remains
   the fallback until parity.
3. **Vulkan RT hybrid** — ray-traced shadows + AO over the raster frame
   (per-frame TLAS rebuild of soldier boxes is standard practice), then
   RT reflections on water/steel. "Ultra" arrives here.

## Meanwhile: the named pain points get fixed in-engine
- V149 removed the pop-in LOD tiers (one model at all distances).
- V152 rebuilds the terrain (see DIRECTION.md).

- V162: in-game Vulkan device boot (src/vkexec.cpp) — instance/device/queue
  live inside openwarband.exe when renderer=vulkan; VulkanExecutorReady()
  is the gate the frame executor will flip.

- V163: in-game Vulkan FRAME EXECUTOR — the battle's recorded buckets render
  on the Vulkan device every frame (offscreen + readback composite over GL).
  Remaining: native Vulkan window/swapchain (removes the readback tax),
  terrain/text through the seam, campaign/town screens, raylib retires.

- V198: NATIVE SWAPCHAIN SHIPPED for battles. A hit-test-transparent child
  window presents the full Vulkan frame (sky underlay + colour terrain +
  army + HUD in one pass, blitted to the acquired image, MAILBOX). No
  readback, no GL composite, no GL content on screen during battles; the
  GL shadow prepass and postfx are skipped natively. 1080p/800 men:
  2.27 ms (441 fps) vs GL 8.31 ms. Fallback chain native -> readback
  bridge -> GL. Remaining for full raylib retirement: campaign/town/menu
  screens (2D ui:: + town 3D), terrain shadows in the native pass
  (meshlit with the sun map), postfx grade as a Vulkan pass, and the
  window itself (input/GLFW) once every screen speaks Vulkan.
