#pragma once
#include "content.h"
#include "raylib.h"

// ---------------------------------------------------------------------------
// Segmented humanoid renderer. Draws any character — player or soldier — from
// its Loadout, so equipping different armour/weapons changes the on-screen
// model automatically. This is the single place body/equipment rendering lives.
// ---------------------------------------------------------------------------

// Transient animation/aim state for one drawn frame.
struct Pose {
    float     yaw       = 0.0f;               // facing, radians (0 = +Z)
    float     swing     = 0.0f;               // 0 idle .. 1 full swing
    AttackDir attackDir = AttackDir::Right;   // which way the current swing goes
    bool      blocking  = false;
    float     walkPhase = 0.0f;               // advances while moving (radians)
};

// Draws a humanoid standing on `feet` (its ground point), wearing `loadout`.
// `teamTint` colours anything not covered by equipment so sides stay readable.
void DrawCharacter(const Content& content, Vector3 feet, const Loadout& loadout,
                   const Pose& pose, Color teamTint);
