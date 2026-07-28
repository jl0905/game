#include "character.h"
#include "../rdr.h"        // PushPill — the pill body style (V179)
#include "../settings.h"   // bodyStyle
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
// Minor parts — boots, neck, stripe, nasal — vanish in the batched tier
// (V132): at that distance they are sub-pixel, and every skipped part is
// one fewer instance for the whole army.
bool Minor() { return Batched(); }

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
// An axis-agnostic box (V149): the one-box body. Batched it rides the sink
// (vertical extent as the a→b axis); direct it is a plain DrawCube — the
// cheapest primitive raylib has.
void Boxy(Vector3 center, float sx, float sy, float sz, Color c) {
    if (Batched()) {
        g_sink({ center.x, center.y - sy * 0.5f, center.z },
               { center.x, center.y + sy * 0.5f, center.z },
               sx / 1.8f, c);
        return;
    }
    DrawCube(center, sx, sy, sz, c);
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
void SwingArc(AttackDir dir, Vector3& cocked, Vector3& follow,
              WeaponClass wc = WeaponClass::OneHanded, bool mounted = false) {
    // V143: the cock is DRASTIC — the blade goes far behind the shoulder
    // line, so which swing is coming reads from across a duel circle.
    switch (dir) {
        case AttackDir::Up:    cocked = { 0.25f, 2.65f, -0.95f }; follow = { 0.0f, 0.25f, 1.6f }; break; // overhead
        case AttackDir::Down:  cocked = { 0.30f, 1.05f, -0.95f }; follow = { 0.1f, 1.15f, 2.1f }; break; // thrust
        case AttackDir::Left:  cocked = { 1.9f, 1.75f, -0.55f };  follow = { -1.5f, 1.05f, 1.1f }; break; // R->L
        case AttackDir::Right: default:
                               cocked = { -1.9f, 1.75f, -0.55f }; follow = { 1.5f, 1.05f, 1.1f }; break; // L->R
    }
    // Mounted arcs (V181, Warband read): each class swings differently from
    // the saddle so the stroke - and its hitbox - telegraphs clearly.
    if (!mounted) return;
    switch (wc) {
        case WeaponClass::OneHanded:   // sabre work: wide, low, sweeping cuts
            if (dir == AttackDir::Left || dir == AttackDir::Right) {
                cocked.x *= 1.25f; cocked.y -= 0.20f;
                follow.x *= 1.35f; follow.y -= 0.35f; follow.z += 0.25f;
            } else if (dir == AttackDir::Up) {
                follow.y -= 0.30f; follow.z += 0.40f;   // saddle chop, down past the horse's shoulder
            } else {
                follow.y -= 0.25f;                       // thrust angles down at footmen
            }
            break;
        case WeaponClass::Polearm:     // the lance: couched low, driven far forward
            if (dir == AttackDir::Down) {
                cocked = { 0.30f, 1.00f, -0.50f };
                follow = { 0.05f, 0.95f, 2.60f };
            } else {                   // lateral polearm work is cramped from a saddle
                cocked.x *= 0.60f;
                follow.x *= 0.60f;
            }
            break;
        case WeaponClass::TwoHanded:   // great blades ride high and fall in a long diagonal
            if (dir == AttackDir::Up) { cocked.y += 0.30f; follow.z += 0.50f; }
            else { follow.y -= 0.20f; follow.x *= 1.15f; }
            break;
        default: break;                // ranged etc: unchanged
    }
}

// The point the blade aims at this frame, in local space, blending rest ->
// cocked (while holding a wind-up) -> follow-through (while swinging).
Vector3 SwingAim(const Pose& pose, WeaponClass wc = WeaponClass::OneHanded) {
    const Vector3 rest{ 0.45f, 1.35f, 0.95f };
    Vector3 cocked, follow;
    SwingArc(pose.attackDir, cocked, follow, wc, pose.mounted);
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
    // Mounted swing lean (V181, Warband read): while winding or striking
    // from the saddle, the whole upper body tilts into the stroke. The lean
    // is applied INSIDE the local transform, scaled by height, so torso,
    // head, arms and weapon all angle together while the seat stays planted
    // - hitboxes read from across the field, for the player and the AI alike.
    Vector3 lean = { 0, 0, 0 };
    if (pose.mounted && (pose.windup > 0.0f || pose.swing > 0.0f)) {
        const int whL = pose.weapon >= 0 ? pose.weapon : loadout.get(EquipSlot::Weapon);
        const WeaponClass wcL = content.weapons.valid(whL)
                                    ? content.weapons[whL].wclass
                                    : WeaponClass::OneHanded;
        const Vector3 aimL = SwingAim(pose, wcL);
        lean.x = Clamp(aimL.x * 0.24f, -0.48f, 0.48f);
        lean.z = pose.swing > 0.0f
                     ? 0.38f * (1.0f - Clamp(pose.swing, 0.0f, 1.0f))
                     : 0.14f * Clamp(pose.windup, 0.0f, 1.0f);
    }
    auto at = [&](float r, float u, float f) {
        // 0 at the seat, 1 at the shoulders; capped so hand/blade points
        // above the shoulder line don't over-amplify the tilt (V182).
        const float t = fminf(u / 1.64f, 1.2f);
        return ToWorld(feet, yaw, r + lean.x * t, u, f + lean.z * t);
    };

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

    // ---- The BODY block (V179: three selectable styles). This is the ONLY
    //      part the bodyStyle setting touches: torso/legs/boots/head. Arms,
    //      shields, weapons, plumes and every scrap of combat/pose logic
    //      below this block are shared by all styles, untouched.
    const float rock = sinf(pose.walkPhase) * 0.06f;   // stride sway
    const int bodyAv = content.armor.valid(loadout.get(EquipSlot::Body))
                           ? content.armor[loadout.get(EquipSlot::Body)].armor : 0;
    // V180: armour is TEXTURE, not geometry — the width bump is gone; the
    // body reads its protection from the skin atlas tier instead.
    const float bw = 0.56f;
    const int bodySkin = bodyAv >= 5 ? 3 : bodyAv >= 3 ? 2 : bodyAv >= 1 ? 1 : 0;
    const int style = GetSettings().bodyStyle;   // 0 boxy | 1 blocky | 2 pill
    const int helmAv = hasHelm && content.armor.valid(loadout.get(EquipSlot::Head))
                           ? content.armor[loadout.get(EquipSlot::Head)].armor : 0;

    if (style == 1) {
        // ---- BLOCKY (V179): the Roblox/Minecraft read — a big cubic head,
        //      a squared torso, and two legs that actually stride. Same
        //      colours, same armour-value width rules as boxy.
        const float legSwing = sinf(pose.walkPhase) * 0.18f;
        for (const float side : { -1.0f, 1.0f }) {
            const float lx = side * bw * 0.24f;
            const float lz = side * legSwing;             // opposite fore-aft
            Boxy(at(lx + rock * 0.3f, 0.50f, lz), bw * 0.42f, 0.90f, 0.34f, bodyC);
            Boxy(at(lx + rock * 0.3f, 0.08f, lz), bw * 0.44f, 0.16f, 0.40f, feetC);
        }
        rdr::PushOrientedBoxSkinned(at(rock * 0.5f, 1.30f - 0.39f, 0.0f),
                                    at(rock * 0.5f, 1.30f + 0.39f, 0.0f),
                                    bw * 1.05f / 1.8f, bodyC, bodySkin);
        Boxy(at(rock * 0.5f, 1.30f, 0.22f), bw * 0.40f, 0.60f, 0.05f,
             flashed(teamTint));                          // surcoat plate
        Boxy(at(0.0f, 1.95f, 0.0f), 0.50f, 0.50f, 0.50f, headC);   // the cube head
        if (hasHelm) {
            if (helmAv >= 2)
                Boxy(at(0.0f, 1.72f, 0.0f), 0.62f, 0.08f, 0.62f, headC);   // brim
            Boxy(at(0.0f, 2.24f, 0.0f), 0.44f, 0.16f, 0.44f, headC);       // dome
            if (helmAv >= 3) {
                Boxy(at(-0.27f, 1.90f, 0.10f), 0.06f, 0.30f, 0.24f, headC);
                Boxy(at( 0.27f, 1.90f, 0.10f), 0.06f, 0.30f, 0.24f, headC);
            }
        }
    } else {
        // ---- ONE SHAPE + A HEAD (V183, user call: "single simple shape for
        //      the whole player model and maybe a head"). No boot band, no
        //      surcoat plate, no helm add-on boxes — the whole silhouette is
        //      one capsule (or one prism in boxy) plus one head, armour from
        //      the skin atlas, team identity blended into the body colour.
        //      (feetC/helmAv keep informing colours, not geometry.)
        (void)feetC;
        (void)helmAv;
        auto teamBlend = [&](Color c) {
            return Color{ (unsigned char)(c.r + (teamTint.r - c.r) / 4),
                          (unsigned char)(c.g + (teamTint.g - c.g) / 4),
                          (unsigned char)(c.b + (teamTint.b - c.b) / 4), c.a };
        };
        const Color bodyOne = flashed(teamBlend(bodyC));
        if (style == 2) {
            // PILL (the default): one capsule, boots to shoulders.
            rdr::PushPillSkinned(at(rock * 0.5f, 0.16f, 0.0f),
                                 at(rock * 0.5f, 1.50f, 0.0f),
                                 0.30f, bodyOne, bodySkin);
            // The head: a sphere; a helm simply makes it a bit bigger and
            // wears the helm's tint — silhouette stays one round shape.
            const float hr = hasHelm ? 0.22f : 0.19f;
            rdr::PushPill(at(0.0f, 1.86f, 0.0f), at(0.0f, 1.86f + 0.001f, 0.0f),
                          hr, headC);
        } else {
            // BOXY: one prism, boots to shoulders, and a head box.
            rdr::PushOrientedBoxSkinned(at(rock * 0.5f, 0.0f, 0.0f),
                                        at(rock * 0.5f, 1.64f, 0.0f),
                                        bw / 1.8f, bodyOne, bodySkin);
            const float hs = hasHelm ? 0.40f : 0.34f;
            Boxy(at(0.0f, 1.87f, 0.0f), hs, hs, hs, headC);
        }
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
    // Upper arm to elbow to forearm (V132): a slight natural bend, deeper
    // when the shield comes up.
    {
        const Vector3 shoulderL = at(-0.32f, 1.52f, 0.0f);
        const Vector3 elbowL    = at(-0.38f, 1.24f + guard * 0.4f, 0.06f + guard * 0.4f);
        const Vector3 handL     = at(-0.36f, 1.05f + guard, 0.22f + guard);
        Cap(shoulderL, elbowL, 0.10f, S(7), R(3), bodyC);
        Cap(elbowL, handL, 0.08f, S(6), R(3), handsC);
    }
    if (pose.blocking) {
        Cyl(at(-0.45f, 1.2f, 0.55f), at(-0.45f, 1.2f, 0.62f), 0.38f, 0.38f, S(14),
                       flashed(DARKBROWN));
        Cyl(at(-0.45f, 1.2f, 0.62f), at(-0.45f, 1.2f, 0.66f), 0.10f, 0.10f, S(8),
                       flashed(GRAY));   // boss
    } else if (oneHanded) {   // carried at the forearm when not raised
        Cyl(at(-0.52f, 1.15f, 0.18f), at(-0.46f, 1.15f, 0.18f), 0.30f, 0.30f, S(12),
                       flashed(DARKBROWN));
    }

    // ---- Weapon line first (V143): the hand FOLLOWS the blade, so the
    //      whole arm cocks, guards and sweeps instead of hanging still ----
    const int wh = pose.weapon >= 0 ? pose.weapon : loadout.get(EquipSlot::Weapon);
    Vector3 wlHand{ 0.42f, 1.15f, 0.15f };
    Vector3 wlTip { 0.42f, 1.15f, 1.55f };
    const bool haveWeapon = content.weapons.valid(wh);
    const bool rangedW = haveWeapon &&
                         content.weapons[wh].wclass == WeaponClass::Ranged;
    if (haveWeapon && !rangedW) {
        const float reach = content.weapons[wh].reach > 0.5f
                                ? content.weapons[wh].reach : 1.4f;
        if (pose.blocking && pose.guardDir >= 0) {
            // Weapon-guard stances (V143): the blade physically bars the
            // line it guards — readable, and readable is counterable.
            switch ((AttackDir)pose.guardDir) {
                case AttackDir::Up:    wlHand = { -0.35f, 1.95f, 0.30f };
                                       wlTip  = {  0.60f, 2.02f, 0.30f }; break; // roof
                case AttackDir::Down:  wlHand = { -0.35f, 0.82f, 0.42f };
                                       wlTip  = {  0.60f, 0.78f, 0.42f }; break; // low bar
                case AttackDir::Left:  wlHand = { -0.52f, 0.95f, 0.32f };
                                       wlTip  = { -0.58f, 1.98f, 0.22f }; break; // hanging left
                case AttackDir::Right:
                default:               wlHand = {  0.58f, 0.95f, 0.32f };
                                       wlTip  = {  0.52f, 1.98f, 0.22f }; break; // hanging right
            }
        } else {
            const int whA = pose.weapon >= 0 ? pose.weapon : loadout.get(EquipSlot::Weapon);
            const Vector3 aim = SwingAim(pose, content.weapons.valid(whA)
                                                   ? content.weapons[whA].wclass
                                                   : WeaponClass::OneHanded);   // V181
            // The hand travels toward the aim point (V143): a cocked
            // overhead pulls the fist high behind the head, a thrust
            // coils it back — the arm shows the direction, drastically.
            wlHand = Vector3Add(wlHand,
                                Vector3Scale(Vector3Subtract(aim, wlHand), 0.30f));
            wlTip = Vector3Add(wlHand,
                               Vector3Scale(Vector3Normalize(
                                                Vector3Subtract(aim, wlHand)),
                                            reach));
        }
    }

    // ---- Right arm (weapon side): shoulder to elbow to the weapon hand ----
    {
        const Vector3 shoulderR = at(0.32f, 1.52f, 0.0f);
        const Vector3 handR     = at(wlHand.x, wlHand.y, wlHand.z);
        // Elbow floats between shoulder and hand, biased outward.
        const Vector3 elbowR = at(0.32f + (wlHand.x - 0.32f) * 0.5f + 0.08f,
                                  1.52f + (wlHand.y - 1.52f) * 0.5f,
                                  (wlHand.z) * 0.5f);
        Cap(shoulderR, elbowR, 0.10f, S(7), R(3), bodyC);
        Cap(elbowR, handR, 0.08f, S(6), R(3), handsC);
    }

    // ---- Weapon (the active one; a character may carry several) ----
    if (content.weapons.valid(wh)) {
        const WeaponDef& w = content.weapons[wh];
        const float reach = w.reach > 0.5f ? w.reach : 1.4f;

        const Vector3 hilt = at(wlHand.x, wlHand.y, wlHand.z);
        const Vector3 tip  = at(wlTip.x, wlTip.y, wlTip.z);

        // (V121) The orange wind-up arc dots are gone by user request — the
        // cocked blade pose itself telegraphs the swing plane well enough.

        // Motion trail (V143: longer and brighter — slow committed arcs
        // deserve a wake the eye can follow).
        if (pose.swing > 0.0f && w.wclass != WeaponClass::Ranged) {
            const float p = 1.0f - Clamp(pose.swing, 0.0f, 1.0f);
            for (int g = 1; g <= 5; ++g) {
                const float gp = Clamp(p - 0.09f * g, 0.0f, 1.0f);
                Pose gpose = pose;
                gpose.swing = 1.0f - gp;
                Vector3 gh, gt;
                BladeLine(SwingAim(gpose), gh, gt, reach);
                Cyl(at(gh.x, gh.y, gh.z), at(gt.x, gt.y, gt.z),
                               0.028f, 0.014f, 6, Fade(w.tint, 0.14f * (6 - g)));
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
