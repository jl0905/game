#include "character.h"
#include "raymath.h"
#include <cmath>

// ---------------------------------------------------------------------------
// Draws a segmented humanoid from its loadout. Body parts are positioned in a
// local frame (x = right, y = up, z = forward) then rotated by the pose yaw so
// the whole figure faces where it aims. Equipment tints come from the content
// registries; anything unarmoured falls back to skin or the team tint.
// ---------------------------------------------------------------------------

namespace {

constexpr Color SKIN{ 214, 176, 142, 255 };

// Tessellation tier (V127): tier 1 halves slices and floors rings — the
// mid-distance ranks keep their silhouette at a fraction of the vertices.
int g_charTier = 0;
int S(int full) { return g_charTier ? (full > 5 ? full / 2 : 4) : full; }
int R(int full) { return g_charTier ? 2 : full; }

// Per-part instancing (V128): with a sink installed and tier 1 active,
// primitives become oriented boxes handed to the caller's batcher.
LimbSink g_sink = nullptr;
bool Batched() { return g_sink != nullptr && g_charTier == 1; }

void Cap(Vector3 a, Vector3 b, float r, int sl, int ri, Color c) {
    if (Batched()) { g_sink(a, b, r, c); return; }
    DrawCapsule(a, b, r, sl, ri, c);
}
void Cyl(Vector3 a, Vector3 b, float r0, float r1, int sl, Color c) {
    if (Batched()) { g_sink(a, b, r0 > r1 ? r0 : r1, c); return; }
    DrawCylinderEx(a, b, r0, r1, sl, c);
}
void Sph(Vector3 p, float r, int ri, int sl, Color c) {
    if (Batched()) { g_sink(p, p, r, c); return; }
    DrawSphereEx(p, r, ri, sl, c);
}

// Local (right, up, fwd) -> world, rotating around Y by yaw about `feet`.
Vector3 ToWorld(Vector3 feet, float yaw, float right, float up, float fwd) {
    const float s = sinf(yaw), c = cosf(yaw);
    return { feet.x + right * c + fwd * s,
             feet.y + up,
             feet.z - right * s + fwd * c };
}

// Tint of an equipped armour slot, or a fallback when the slot is empty.
Color SlotTint(const Content& content, const Loadout& lo, EquipSlot slot, Color fallback) {
    const int h = lo.get(slot);
    return content.armor.valid(h) ? content.armor[h].tint : fallback;
}

// The wind-up ("cocked") and follow-through offsets for each attack direction,
// in local (right, up, fwd) space. Overhead comes from high/back and lands low
// front; a thrust pulls back then extends forward; side cuts sweep across.
void SwingArc(AttackDir dir, Vector3& cocked, Vector3& follow) {
    switch (dir) {
        case AttackDir::Up:    cocked = { 0.1f, 2.3f, -0.5f }; follow = { 0.0f, 0.3f, 1.5f }; break; // overhead
        case AttackDir::Down:  cocked = { 0.2f, 1.2f, -0.4f }; follow = { 0.1f, 1.1f, 2.0f }; break; // thrust
        case AttackDir::Left:  cocked = { 1.5f, 1.6f, -0.1f }; follow = { -1.4f, 1.1f, 1.0f }; break; // R->L
        case AttackDir::Right: default:
                               cocked = { -1.5f, 1.6f, -0.1f }; follow = { 1.4f, 1.1f, 1.0f }; break; // L->R
    }
}

// The point the blade aims at this frame, in local space, blending rest ->
// cocked (while holding a wind-up) -> follow-through (while swinging).
Vector3 SwingAim(const Pose& pose) {
    const Vector3 rest{ 0.45f, 1.35f, 0.95f };
    Vector3 cocked, follow;
    SwingArc(pose.attackDir, cocked, follow);
    if (pose.swing > 0.0f) {
        float p = 1.0f - Clamp(pose.swing, 0.0f, 1.0f);   // 0 at strike -> 1 done
        p = p * p * (3.0f - 2.0f * p);                    // ease
        return Vector3Lerp(cocked, follow, p);
    }
    if (pose.windup > 0.0f)
        return Vector3Lerp(rest, cocked, Clamp(pose.windup, 0.0f, 1.0f));
    return rest;
}

// Blade hilt + tip for a given aim point, along the arm from the hand.
void BladeLine(const Vector3& aim, Vector3& hilt, Vector3& tip, float reach) {
    const Vector3 hand{ 0.42f, 1.15f, 0.15f };
    hilt = hand;
    tip  = Vector3Add(hand, Vector3Scale(Vector3Normalize(Vector3Subtract(aim, hand)),
                                         reach > 0.5f ? reach : 1.4f));
}

}  // namespace

void SetCharacterDetail(int tier) { g_charTier = tier; }
void SetCharacterBatcher(LimbSink sink) { g_sink = sink; }

void DrawCharacter(const Content& content, Vector3 feet, const Loadout& loadout,
                   const Pose& pose, Color teamTint) {
    const float yaw = pose.yaw;
    auto at = [&](float r, float u, float f) { return ToWorld(feet, yaw, r, u, f); };

    // Just-hit feedback: everything flares toward white for a few frames.
    const float fl = Clamp(pose.flash, 0.0f, 1.0f);
    auto flashed = [&](Color c) {
        return fl <= 0.0f ? c
                          : Color{ (unsigned char)(c.r + (255 - c.r) * fl),
                                   (unsigned char)(c.g + (255 - c.g) * fl),
                                   (unsigned char)(c.b + (255 - c.b) * fl), c.a };
    };

    const Color bodyC   = flashed(SlotTint(content, loadout, EquipSlot::Body, teamTint));
    const Color feetC   = flashed(SlotTint(content, loadout, EquipSlot::Feet, DARKBROWN));
    const Color handsC  = flashed(SlotTint(content, loadout, EquipSlot::Hands, SKIN));
    const bool  hasHelm = loadout.has(EquipSlot::Head);
    const Color headC   = flashed(hasHelm ? SlotTint(content, loadout, EquipSlot::Head, SKIN) : SKIN);

    // Walk cycle: legs swing fore/aft, arms counter-swing.
    const float legSwing = sinf(pose.walkPhase) * 0.35f;

    // ---- Legs ----
    Cap(at(-0.16f, 0.05f,  legSwing), at(-0.16f, 0.95f, 0.0f), 0.14f, S(8), R(4), feetC);
    Cap(at( 0.16f, 0.05f, -legSwing), at( 0.16f, 0.95f, 0.0f), 0.14f, S(8), R(4), feetC);

    // ---- Torso (+ a team surcoat stripe down the chest so sides read) ----
    Cap(at(0.0f, 0.95f, 0.0f), at(0.0f, 1.6f, 0.0f), 0.30f, S(10), R(6), bodyC);
    Cap(at(0.0f, 1.0f, 0.24f), at(0.0f, 1.55f, 0.24f), 0.09f, S(6), R(4), flashed(teamTint));

    // ---- Head: dome helmet with a nasal bar, or a bare head ----
    Sph(at(0.0f, 1.85f, 0.0f), 0.22f, R(16), S(16), headC);
    if (hasHelm) {
        Cyl(at(0.0f, 1.72f, 0.0f), at(0.0f, 1.80f, 0.0f), 0.27f, 0.27f, S(10), headC); // brim
        Cyl(at(0.0f, 1.86f, 0.0f), at(0.0f, 2.08f, 0.0f), 0.22f, 0.05f, S(10), headC); // dome
        Cap(at(0.0f, 1.90f, 0.24f), at(0.0f, 1.74f, 0.26f), 0.03f, S(4), R(3), headC);       // nasal
    }
    // Troop plume: rank/type identity at a glance (accent alpha 0 = none).
    if (pose.accent.a > 0)
        Cap(at(0.0f, 2.05f, -0.05f), at(0.0f, 2.30f, -0.18f), 0.05f, S(5), R(3),
                    flashed(pose.accent));

    // ---- Left arm + shield ----
    // Sword-and-board troops carry the shield always; everyone raises it while
    // guarding (blocking pulls it up front and centre).
    const int   whShield  = pose.weapon >= 0 ? pose.weapon : loadout.get(EquipSlot::Weapon);
    const bool  oneHanded = content.weapons.valid(whShield) &&
                            content.weapons[whShield].wclass == WeaponClass::OneHanded;
    const float guard = pose.blocking ? 0.6f : 0.0f;
    Cap(at(-0.34f, 1.5f, 0.0f), at(-0.34f, 1.05f + guard, 0.2f + guard), 0.11f, S(8), R(4), bodyC);
    if (pose.blocking) {
        Cyl(at(-0.45f, 1.2f, 0.55f), at(-0.45f, 1.2f, 0.62f), 0.38f, 0.38f, S(14),
                       flashed(DARKBROWN));
        Cyl(at(-0.45f, 1.2f, 0.62f), at(-0.45f, 1.2f, 0.66f), 0.10f, 0.10f, S(8),
                       flashed(GRAY));   // boss
    } else if (oneHanded) {   // carried at the forearm when not raised
        Cyl(at(-0.52f, 1.15f, 0.18f), at(-0.46f, 1.15f, 0.18f), 0.30f, 0.30f, S(12),
                       flashed(DARKBROWN));
    }

    // ---- Right arm (weapon side) ----
    Cap(at(0.34f, 1.5f, 0.0f), at(0.42f, 1.15f, 0.15f), 0.11f, S(8), R(4), handsC);

    // ---- Weapon (the active one; a character may carry several) ----
    const int wh = pose.weapon >= 0 ? pose.weapon : loadout.get(EquipSlot::Weapon);
    if (content.weapons.valid(wh)) {
        const WeaponDef& w = content.weapons[wh];
        const float reach = w.reach > 0.5f ? w.reach : 1.4f;

        const Vector3 aim = SwingAim(pose);
        Vector3 lh, lt;
        BladeLine(aim, lh, lt, reach);
        const Vector3 hilt = at(lh.x, lh.y, lh.z);
        const Vector3 tip  = at(lt.x, lt.y, lt.z);

        // (V121) The orange wind-up arc dots are gone by user request — the
        // cocked blade pose itself telegraphs the swing plane well enough.

        // Motion trail: faint ghosts of the blade slightly earlier in the arc.
        if (pose.swing > 0.0f && w.wclass != WeaponClass::Ranged) {
            const float p = 1.0f - Clamp(pose.swing, 0.0f, 1.0f);
            for (int g = 1; g <= 3; ++g) {
                const float gp = Clamp(p - 0.10f * g, 0.0f, 1.0f);
                Pose gpose = pose;
                gpose.swing = 1.0f - gp;
                Vector3 gh, gt;
                BladeLine(SwingAim(gpose), gh, gt, reach);
                Cyl(at(gh.x, gh.y, gh.z), at(gt.x, gt.y, gt.z),
                               0.02f, 0.01f, 6, Fade(w.tint, 0.18f * (4 - g)));
            }
        }

        switch (w.wclass) {
            case WeaponClass::Polearm: {
                Cyl(hilt, tip, 0.035f, 0.035f, 6, Color{ 110, 78, 48, 255 }); // shaft
                const Vector3 dir = Vector3Normalize(Vector3Subtract(tip, hilt));
                Cyl(tip, Vector3Add(tip, Vector3Scale(dir, 0.35f)), 0.06f, 0.0f, 6, w.tint); // head
                break;
            }
            case WeaponClass::Ranged:
                // A simple bow held vertically in the hand.
                Cyl(at(0.46f, 1.65f, 0.2f), at(0.46f, 0.65f, 0.2f), 0.03f, 0.03f, 6, w.tint);
                DrawLine3D(at(0.46f, 1.65f, 0.2f), at(0.46f, 0.65f, 0.2f), Fade(RAYWHITE, 0.6f)); // string
                break;
            case WeaponClass::Axe: {
                // A haft with a broad head set just below the tip.
                const Vector3 dir = Vector3Normalize(Vector3Subtract(tip, hilt));
                const Vector3 side = { dir.z, 0, -dir.x };
                const Vector3 neck = Vector3Add(hilt, Vector3Scale(dir,
                                        Vector3Distance(hilt, tip) * 0.8f));
                Cyl(hilt, tip, 0.035f, 0.03f, 6, Color{ 110, 78, 48, 255 }); // haft
                Cyl(Vector3Subtract(neck, Vector3Scale(side, 0.05f)),
                               Vector3Add(neck, Vector3Scale(side, 0.30f)),
                               0.16f, 0.05f, 6, w.tint);                                 // blade
                break;
            }
            case WeaponClass::TwoHanded: {
                const Vector3 dir = Vector3Normalize(Vector3Subtract(tip, hilt));
                const Vector3 guard = { dir.z, 0, -dir.x };  // crossguard, perpendicular
                Cyl(hilt, tip, 0.06f, 0.03f, 8, w.tint);
                Cyl(Vector3Subtract(hilt, Vector3Scale(guard, 0.22f)),
                               Vector3Add(hilt, Vector3Scale(guard, 0.22f)), 0.035f, 0.035f, 6, DARKGRAY);
                break;
            }
            case WeaponClass::OneHanded:
            default: {
                const Vector3 dir = Vector3Normalize(Vector3Subtract(tip, hilt));
                const Vector3 guard = { dir.z, 0, -dir.x };
                Cyl(hilt, tip, 0.05f, 0.02f, 8, w.tint);
                Cyl(Vector3Subtract(hilt, Vector3Scale(guard, 0.16f)),
                               Vector3Add(hilt, Vector3Scale(guard, 0.16f)), 0.03f, 0.03f, 6, DARKGRAY);
                break;
            }
        }
    }

    // ---- Team banner accent (small marker above head) ----
    Sph(at(0.0f, 2.2f, 0.0f), 0.07f, R(8), S(8), teamTint);
}
