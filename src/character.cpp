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

// Where the weapon hand sits and points during a swing, in local space. Returns
// hilt and tip so any weapon class can be drawn along that line.
void SwingLine(const Pose& pose, Vector3& hilt, Vector3& tip, float reach) {
    // Rest pose: weapon held forward from the right hand.
    const Vector3 hand{ 0.42f, 1.15f, 0.15f };
    const float p = Clamp(pose.swing, 0.0f, 1.0f);
    // A swing arcs from a wind-up offset to a follow-through offset.
    Vector3 from, to;
    switch (pose.attackDir) {
        case AttackDir::Up:    from = { 0.2f, 2.1f, -0.2f }; to = { 0.0f, 0.4f, 1.2f }; break; // overhead
        case AttackDir::Down:  from = { 0.1f, 0.6f, 0.4f };  to = { 0.1f, 1.4f, 1.6f }; break; // thrust
        case AttackDir::Left:  from = { 1.2f, 1.4f, 0.2f };  to = { -1.2f, 1.2f, 0.9f }; break; // R->L
        case AttackDir::Right: default:
                               from = { -1.2f, 1.4f, 0.2f }; to = { 1.2f, 1.2f, 0.9f }; break;  // L->R
    }
    const Vector3 aim = (p > 0.0f) ? Vector3Lerp(from, to, p) : Vector3{ 0, 1.4f, 1.0f };
    hilt = hand;
    tip  = Vector3Add(hand, Vector3Scale(Vector3Normalize(Vector3Subtract(aim, hand)),
                                         reach > 0.5f ? reach : 1.4f));
}

}  // namespace

void DrawCharacter(const Content& content, Vector3 feet, const Loadout& loadout,
                   const Pose& pose, Color teamTint) {
    const float yaw = pose.yaw;
    auto at = [&](float r, float u, float f) { return ToWorld(feet, yaw, r, u, f); };

    const Color bodyC   = SlotTint(content, loadout, EquipSlot::Body, teamTint);
    const Color feetC   = SlotTint(content, loadout, EquipSlot::Feet, DARKBROWN);
    const Color handsC  = SlotTint(content, loadout, EquipSlot::Hands, SKIN);
    const bool  hasHelm = loadout.has(EquipSlot::Head);
    const Color headC   = hasHelm ? SlotTint(content, loadout, EquipSlot::Head, SKIN) : SKIN;

    // Walk cycle: legs swing fore/aft, arms counter-swing.
    const float legSwing = sinf(pose.walkPhase) * 0.35f;

    // ---- Legs ----
    DrawCapsule(at(-0.16f, 0.05f,  legSwing), at(-0.16f, 0.95f, 0.0f), 0.14f, 8, 4, feetC);
    DrawCapsule(at( 0.16f, 0.05f, -legSwing), at( 0.16f, 0.95f, 0.0f), 0.14f, 8, 4, feetC);

    // ---- Torso ----
    DrawCapsule(at(0.0f, 0.95f, 0.0f), at(0.0f, 1.6f, 0.0f), 0.30f, 10, 6, bodyC);

    // ---- Head (+ helmet tint) ----
    DrawSphere(at(0.0f, 1.85f, 0.0f), 0.22f, headC);
    if (hasHelm)  // a small brim so helmets read as helmets
        DrawCylinderEx(at(0.0f, 1.72f, 0.0f), at(0.0f, 1.78f, 0.0f), 0.26f, 0.26f, 10, headC);

    // ---- Left arm (shield/guard side) ----
    const float guard = pose.blocking ? 0.6f : 0.0f;
    DrawCapsule(at(-0.34f, 1.5f, 0.0f), at(-0.34f, 1.05f + guard, 0.2f + guard), 0.11f, 8, 4, bodyC);
    if (pose.blocking)  // raise a simple round shield when guarding
        DrawCylinderEx(at(-0.45f, 1.2f, 0.55f), at(-0.45f, 1.2f, 0.62f), 0.38f, 0.38f, 14, DARKBROWN);

    // ---- Right arm (weapon side) ----
    DrawCapsule(at(0.34f, 1.5f, 0.0f), at(0.42f, 1.15f, 0.15f), 0.11f, 8, 4, handsC);

    // ---- Weapon ----
    const int wh = loadout.get(EquipSlot::Weapon);
    if (content.weapons.valid(wh)) {
        const WeaponDef& w = content.weapons[wh];
        Vector3 lh, lt;
        const float reach = w.reach > 0.5f ? w.reach : 1.4f;
        SwingLine(pose, lh, lt, reach);
        const Vector3 hilt = at(lh.x, lh.y, lh.z);
        const Vector3 tip  = at(lt.x, lt.y, lt.z);
        switch (w.wclass) {
            case WeaponClass::Polearm:
                DrawCylinderEx(hilt, tip, 0.03f, 0.03f, 6, w.tint);
                DrawCylinderEx(tip, Vector3Add(tip, Vector3Scale(Vector3Normalize(Vector3Subtract(tip, hilt)), 0.25f)),
                               0.05f, 0.0f, 6, GRAY);  // spearhead
                break;
            case WeaponClass::Ranged:
                DrawCylinderEx(at(0.42f, 1.6f, 0.2f), at(0.42f, 0.7f, 0.2f), 0.03f, 0.03f, 6, w.tint);
                break;
            case WeaponClass::TwoHanded:
                DrawCylinderEx(hilt, tip, 0.06f, 0.03f, 6, w.tint);
                break;
            case WeaponClass::OneHanded:
            default:
                DrawCylinderEx(hilt, tip, 0.05f, 0.02f, 6, w.tint);
                break;
        }
    }

    // ---- Team banner accent (small marker above head) ----
    DrawSphere(at(0.0f, 2.2f, 0.0f), 0.07f, teamTint);
}
