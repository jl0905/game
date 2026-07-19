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
    DrawCapsule(at(-0.16f, 0.05f,  legSwing), at(-0.16f, 0.95f, 0.0f), 0.14f, 8, 4, feetC);
    DrawCapsule(at( 0.16f, 0.05f, -legSwing), at( 0.16f, 0.95f, 0.0f), 0.14f, 8, 4, feetC);

    // ---- Torso (+ a team surcoat stripe down the chest so sides read) ----
    DrawCapsule(at(0.0f, 0.95f, 0.0f), at(0.0f, 1.6f, 0.0f), 0.30f, 10, 6, bodyC);
    DrawCapsule(at(0.0f, 1.0f, 0.24f), at(0.0f, 1.55f, 0.24f), 0.09f, 6, 4, flashed(teamTint));

    // ---- Head: dome helmet with a nasal bar, or a bare head ----
    DrawSphere(at(0.0f, 1.85f, 0.0f), 0.22f, headC);
    if (hasHelm) {
        DrawCylinderEx(at(0.0f, 1.72f, 0.0f), at(0.0f, 1.80f, 0.0f), 0.27f, 0.27f, 10, headC); // brim
        DrawCylinderEx(at(0.0f, 1.86f, 0.0f), at(0.0f, 2.08f, 0.0f), 0.22f, 0.05f, 10, headC); // dome
        DrawCapsule(at(0.0f, 1.90f, 0.24f), at(0.0f, 1.74f, 0.26f), 0.03f, 4, 3, headC);       // nasal
    }
    // Troop plume: rank/type identity at a glance (accent alpha 0 = none).
    if (pose.accent.a > 0)
        DrawCapsule(at(0.0f, 2.05f, -0.05f), at(0.0f, 2.30f, -0.18f), 0.05f, 5, 3,
                    flashed(pose.accent));

    // ---- Left arm + shield ----
    // Sword-and-board troops carry the shield always; everyone raises it while
    // guarding (blocking pulls it up front and centre).
    const int   whShield  = pose.weapon >= 0 ? pose.weapon : loadout.get(EquipSlot::Weapon);
    const bool  oneHanded = content.weapons.valid(whShield) &&
                            content.weapons[whShield].wclass == WeaponClass::OneHanded;
    const float guard = pose.blocking ? 0.6f : 0.0f;
    DrawCapsule(at(-0.34f, 1.5f, 0.0f), at(-0.34f, 1.05f + guard, 0.2f + guard), 0.11f, 8, 4, bodyC);
    if (pose.blocking) {
        DrawCylinderEx(at(-0.45f, 1.2f, 0.55f), at(-0.45f, 1.2f, 0.62f), 0.38f, 0.38f, 14,
                       flashed(DARKBROWN));
        DrawCylinderEx(at(-0.45f, 1.2f, 0.62f), at(-0.45f, 1.2f, 0.66f), 0.10f, 0.10f, 8,
                       flashed(GRAY));   // boss
    } else if (oneHanded) {   // carried at the forearm when not raised
        DrawCylinderEx(at(-0.52f, 1.15f, 0.18f), at(-0.46f, 1.15f, 0.18f), 0.30f, 0.30f, 12,
                       flashed(DARKBROWN));
    }

    // ---- Right arm (weapon side) ----
    DrawCapsule(at(0.34f, 1.5f, 0.0f), at(0.42f, 1.15f, 0.15f), 0.11f, 8, 4, handsC);

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

        // Wind-up telegraph: a faint ghost of the FULL upcoming arc, so the
        // swing plane (and how to block it) is readable before it lands.
        if (pose.windup > 0.05f && pose.swing <= 0.0f && w.wclass != WeaponClass::Ranged) {
            Vector3 cocked, follow;
            SwingArc(pose.attackDir, cocked, follow);
            const int STEPS = 6;
            for (int gi = 0; gi <= STEPS; ++gi) {
                const float p = (float)gi / STEPS;
                Vector3 gh, gt;
                BladeLine(Vector3Lerp(cocked, follow, p), gh, gt, reach);
                DrawSphere(at(gt.x, gt.y, gt.z), 0.035f,
                           Fade(ORANGE, 0.35f * pose.windup));
            }
        }

        // Motion trail: faint ghosts of the blade slightly earlier in the arc.
        if (pose.swing > 0.0f && w.wclass != WeaponClass::Ranged) {
            const float p = 1.0f - Clamp(pose.swing, 0.0f, 1.0f);
            for (int g = 1; g <= 3; ++g) {
                const float gp = Clamp(p - 0.10f * g, 0.0f, 1.0f);
                Pose gpose = pose;
                gpose.swing = 1.0f - gp;
                Vector3 gh, gt;
                BladeLine(SwingAim(gpose), gh, gt, reach);
                DrawCylinderEx(at(gh.x, gh.y, gh.z), at(gt.x, gt.y, gt.z),
                               0.02f, 0.01f, 6, Fade(w.tint, 0.18f * (4 - g)));
            }
        }

        switch (w.wclass) {
            case WeaponClass::Polearm: {
                DrawCylinderEx(hilt, tip, 0.035f, 0.035f, 6, Color{ 110, 78, 48, 255 }); // shaft
                const Vector3 dir = Vector3Normalize(Vector3Subtract(tip, hilt));
                DrawCylinderEx(tip, Vector3Add(tip, Vector3Scale(dir, 0.35f)), 0.06f, 0.0f, 6, w.tint); // head
                break;
            }
            case WeaponClass::Ranged:
                // A simple bow held vertically in the hand.
                DrawCylinderEx(at(0.46f, 1.65f, 0.2f), at(0.46f, 0.65f, 0.2f), 0.03f, 0.03f, 6, w.tint);
                DrawLine3D(at(0.46f, 1.65f, 0.2f), at(0.46f, 0.65f, 0.2f), Fade(RAYWHITE, 0.6f)); // string
                break;
            case WeaponClass::TwoHanded: {
                const Vector3 dir = Vector3Normalize(Vector3Subtract(tip, hilt));
                const Vector3 guard = { dir.z, 0, -dir.x };  // crossguard, perpendicular
                DrawCylinderEx(hilt, tip, 0.06f, 0.03f, 8, w.tint);
                DrawCylinderEx(Vector3Subtract(hilt, Vector3Scale(guard, 0.22f)),
                               Vector3Add(hilt, Vector3Scale(guard, 0.22f)), 0.035f, 0.035f, 6, DARKGRAY);
                break;
            }
            case WeaponClass::OneHanded:
            default: {
                const Vector3 dir = Vector3Normalize(Vector3Subtract(tip, hilt));
                const Vector3 guard = { dir.z, 0, -dir.x };
                DrawCylinderEx(hilt, tip, 0.05f, 0.02f, 8, w.tint);
                DrawCylinderEx(Vector3Subtract(hilt, Vector3Scale(guard, 0.16f)),
                               Vector3Add(hilt, Vector3Scale(guard, 0.16f)), 0.03f, 0.03f, 6, DARKGRAY);
                break;
            }
        }
    }

    // ---- Team banner accent (small marker above head) ----
    DrawSphere(at(0.0f, 2.2f, 0.0f), 0.07f, teamTint);
}
