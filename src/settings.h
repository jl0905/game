#pragma once

// ---------------------------------------------------------------------------
// Player-facing options (direction J4), loaded once at startup from the
// moddable assets/settings.cfg (key value lines; see that file for the
// format). Simulation code must not read these — they cover presentation and
// input comfort only, so headless runs behave identically at any setting.
// ---------------------------------------------------------------------------

struct Settings {
    int   windowWidth  = 1280;
    int   windowHeight = 720;
    bool  fullscreen   = false;
    float lodDistance  = 45.0f;   // soldiers beyond this draw as silhouettes
    bool  particles    = true;    // blood / hoof-dust puffs
    float masterVolume = 1.0f;    // 0..1
    bool  invertY      = false;   // flip vertical mouse look
    float textScale    = 1.4f;    // global lettering size (V72), 1.0–1.6
    float battleSize   = 200.0f;  // field-battle cap per contingent (V75);
                                  // the overflow arrives as reinforcements
    bool  shadows      = true;    // V153: sun shadow mapping in battles
    int   renderer     = 1;       // V161: 0 = raylib (GL), 1 = vulkan — the
                                  // seam (src/rdr.h) picks its executor.
                                  // V192 (user call): vulkan is the DEFAULT;
                                  // it still falls back to GL automatically
                                  // if the executor can't boot.
    bool  postFx       = true;    // V151: filmic post pass (tone map, grade,
                                  // vignette, grain) over the 3D scenes
    bool  ironman      = false;   // V147: OFF = the lone hero always survives
                                  // (no game over); ON = a destroyed warband
                                  // ends the campaign, Warband-permadeath.
    int   bodyStyle    = 2;   // pill is the default look (V186, user call)
                                  // V179: body MODEL only — 0 = boxy (V149
                                  // one-box), 1 = blocky (Minecraft-style
                                  // limbs), 2 = pill (capsule). Arms, weapons
                                  // and combat logic identical in all three.
};

// The live settings. Defaults above until LoadSettings() has run.
Settings& GetSettings();

// Overlay assets/settings.cfg onto the defaults (missing file/keys keep them).
void LoadSettings();

// Write the current settings back to the cfg they were loaded from (or the
// default assets path). Called when the settings screen closes (K1).
void SaveSettings();
