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
};

// The live settings. Defaults above until LoadSettings() has run.
Settings& GetSettings();

// Overlay assets/settings.cfg onto the defaults (missing file/keys keep them).
void LoadSettings();

// Write the current settings back to the cfg they were loaded from (or the
// default assets path). Called when the settings screen closes (K1).
void SaveSettings();
