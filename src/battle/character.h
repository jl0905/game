#pragma once
#include "../content.h"
#include "raylib.h"

// ---------------------------------------------------------------------------
// Segmented humanoid renderer. Draws any character — player or soldier — from
// its Loadout, so equipping different armour/weapons changes the on-screen
// model automatically. This is the single place body/equipment rendering lives.
// ---------------------------------------------------------------------------

// Transient animation/aim state for one drawn frame.
struct Pose {
    float     yaw       = 0.0f;               // facing, radians (0 = +Z)
    float     swing     = 0.0f;               // 1 at strike start .. 0 finished
    float     windup    = 0.0f;               // 0 relaxed .. 1 fully cocked (held)
    AttackDir attackDir = AttackDir::Right;   // which way the swing/hold goes
    bool      blocking  = false;
    float     walkPhase = 0.0f;               // advances while moving (radians)
    int       weapon    = -1;                 // weapon handle to draw; -1 = use
                                              // the loadout's Weapon slot
    float     flash     = 0.0f;               // 0..1 just-hit feedback (white flare)
    Color     accent    = { 0, 0, 0, 0 };     // troop plume colour; alpha 0 = none
};

// Draws a humanoid standing on `feet` (its ground point), wearing `loadout`.
// `teamTint` colours anything not covered by equipment so sides stay readable.
void DrawCharacter(const Content& content, Vector3 feet, const Loadout& loadout,
                   const Pose& pose, Color teamTint);
