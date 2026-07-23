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

// Tessellation tier (V127): 0 = full detail (hero, town NPCs, close ranks),
// 1 = roughly half the slices/rings for soldiers past half the LOD line —
// the silhouette is identical, the vertex bill is not. Sticky until changed.
void SetCharacterDetail(int tier);

// Per-part instancing hook (V128): when a batcher is installed AND the
// detail tier is 1, every limb/weapon primitive is emitted as one oriented
// box (a→b, radius, colour) into the sink instead of an immediate raylib
// call — the caller batches them into instanced draws. Null restores
// direct drawing. A degenerate a==b box is a sphere-ish blob (the head).
using LimbSink = void (*)(Vector3 a, Vector3 b, float r, Color c);
void SetCharacterBatcher(LimbSink sink);

// Draws a humanoid standing on `feet` (its ground point), wearing `loadout`.
// `teamTint` colours anything not covered by equipment so sides stay readable.
void DrawCharacter(const Content& content, Vector3 feet, const Loadout& loadout,
                   const Pose& pose, Color teamTint);
