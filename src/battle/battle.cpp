#include "battle.h"
#include "character.h"
#include "../ui.h"
#include "../parallel.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// Real-time 3D battle. You fight alongside your troops in third person.
//   WASD move, mouse look, SPACE jump.
//   LMB attack — the swing DIRECTION follows your mouse motion (Mount & Blade
//   style): flick up = overhead, down = thrust, left/right = side cuts.
//   RMB block.
// The battle ends when one side is wiped out, or you fall. Casualties and the
// outcome carry back to the campaign map.
// ---------------------------------------------------------------------------

namespace {

constexpr float ARENA          = 90.0f;   // half-size of battlefield
constexpr float SOLDIER_RADIUS = 0.6f;

// Fallbacks when a combatant has no (valid) weapon. TODO(balance).
constexpr float FIST_DAMAGE = 10.0f;
constexpr float FIST_REACH  = 2.6f;
constexpr float FIST_SWING  = 0.7f;

// Missile flight. TODO(balance): gravity/lifetime are feel, not tuning.
constexpr float ARROW_GRAVITY  = 10.0f;
constexpr float ARROW_LIFETIME = 4.0f;
constexpr float ARROW_HIT_RADIUS = 0.9f;
constexpr float BLOCK_MELEE_FACTOR   = 0.15f;  // guarded melee damage multiplier
constexpr float BLOCK_MISSILE_FACTOR = 0.5f;   // a raised guard helps less vs arrows

// Siege walls (roadmap B3b): towns/castles defend behind a wall with one gate.
constexpr float WALL_Z      = 8.0f;   // wall line, between the two spawn lines
constexpr float WALL_BAND   = 1.4f;   // half-thickness of the blocked band
constexpr float WALL_HEIGHT = 4.0f;
constexpr float GATE_HALF   = 5.0f;   // half-width of the gate opening

// ===========================================================================
// Battle terrain
//
// A lightweight, deterministic heightfield: hills/mountains are smooth radial
// bumps, plus one optional carved river and scattered trees. A single analytic
// HeightAt() is the source of truth for BOTH rendering and standing units on
// the ground, so units always match the visible surface. The config is derived
// from the world-map position (TerrainConfigFromWorld) so a fight in the hills
// looks like the hills. Deliberately simple; none of this is game balance.
// ===========================================================================

struct TerrainConfig {
    unsigned int seed          = 1u;    // same seed -> same terrain
    float        hilliness     = 0.4f;  // 0..1 : hill count + amplitude
    float        forestDensity = 0.3f;  // 0..1 : how many trees
    bool         hasRiver      = false;
    bool         mountainous   = false; // adds a few tall peaks
};

struct Tree {
    Vector3 pos;      // trunk base, already on the terrain surface
    float   height;
    float   radius;
    Color   foliage;
};

// Self-contained PRNG so terrain is deterministic per seed and never disturbs
// raylib's global RNG (which the campaign uses for loot, etc.).
struct TerrainRng {
    unsigned int s;
    unsigned int next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float unit() { return (next() & 0xFFFFFF) / static_cast<float>(0xFFFFFF); }
    float range(float a, float b) { return a + (b - a) * unit(); }
};

float SmoothStep(float t) {
    t = Clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Colour bands for the surface, chosen by height and steepness. Purely visual.
Color SurfaceColor(float height, float slope, float shade) {
    Color c;
    if (slope > 0.55f)        c = Color{ 120, 115, 110, 255 };   // exposed rock
    else if (height < 2.5f)   c = Color{  82, 130,  62, 255 };   // grass
    else if (height < 7.0f)   c = Color{ 110,  96,  66, 255 };   // dirt / scrub
    else if (height < 13.0f)  c = Color{ 120, 118, 122, 255 };   // rock
    else                      c = Color{ 232, 234, 240, 255 };   // snow cap
    c.r = static_cast<unsigned char>(c.r * shade);
    c.g = static_cast<unsigned char>(c.g * shade);
    c.b = static_cast<unsigned char>(c.b * shade);
    return c;
}

// Flat-shaded colour for a triangle from its three corners.
Color TriangleColor(Vector3 a, Vector3 b, Vector3 c) {
    const Vector3 n = Vector3Normalize(
        Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
    const float slope = 1.0f - fabsf(n.y);
    const float h = (a.y + b.y + c.y) / 3.0f;
    const float shade = 0.75f + 0.25f * fabsf(n.y);   // face the light a little
    return SurfaceColor(h, slope, shade);
}

// Build a config from a campaign-map position. Same spot -> same battlefield;
// low-frequency noise makes biomes vary smoothly across the map.
TerrainConfig TerrainConfigFromWorld(Vector2 campaignPos) {
    TerrainConfig cfg;

    const int qx = static_cast<int>(campaignPos.x);
    const int qy = static_cast<int>(campaignPos.y);
    unsigned int seed = static_cast<unsigned int>(qx * 73856093) ^
                        static_cast<unsigned int>(qy * 19349663) ^ 0x9E3779B9u;
    cfg.seed = seed ? seed : 1u;

    const float n1 = sinf(campaignPos.x * 0.0031f) * cosf(campaignPos.y * 0.0027f);
    const float n2 = sinf(campaignPos.x * 0.0012f + campaignPos.y * 0.0019f);
    cfg.hilliness     = Clamp(0.5f + 0.5f * n1, 0.0f, 1.0f);
    cfg.forestDensity = Clamp(0.5f + 0.5f * n2, 0.0f, 1.0f);
    cfg.mountainous   = cfg.hilliness > 0.72f;
    cfg.hasRiver      = n2 < -0.15f;
    return cfg;
}

class Terrain {
public:
    void  Generate(const TerrainConfig& cfg, float arenaHalf);
    float HeightAt(float x, float z) const;      // world ground height
    bool  IsWaterAt(float x, float z) const;      // inside the river channel?
    void  Draw() const;                           // call inside BeginMode3D

private:
    struct Hill { Vector2 center; float radius; float height; };

    float RawHeight(float x, float z) const;      // hills/mountains only
    float RiverDistance(float x, float z) const;
    int   gridIndex(int i, int j) const { return i + j * (gridN_ + 1); }

    float arena_      = 90.0f;
    float waterLevel_ = 0.0f;
    bool    hasRiver_       = false;
    Vector2 riverA_{}, riverB_{};
    float   riverHalfWidth_ = 0.0f;
    std::vector<Hill> hills_;
    std::vector<Tree> trees_;

    int                        gridN_ = 0;
    float                      cell_  = 0.0f;
    std::vector<float>         gridH_;
    std::vector<unsigned char> gridWater_;

    // The surface is baked into a GPU mesh on first Draw (windowed only) and
    // drawn as one model instead of thousands of immediate triangles.
    void BakeModel() const;
    mutable Model model_{};
    mutable bool  baked_ = false;
};

void Terrain::Generate(const TerrainConfig& cfg, float arenaHalf) {
    if (baked_) {                      // regenerating: drop the old GPU mesh
        UnloadModel(model_);
        model_ = Model{};
        baked_ = false;
    }
    arena_      = arenaHalf;
    waterLevel_ = 0.0f;
    hasRiver_   = cfg.hasRiver;
    hills_.clear();
    trees_.clear();

    TerrainRng rng{ cfg.seed ? cfg.seed : 1u };

    // hills
    const int hillCount = 2 + static_cast<int>(cfg.hilliness * 6.0f);
    for (int i = 0; i < hillCount; ++i) {
        Hill h;
        h.center = { rng.range(-arena_, arena_), rng.range(-arena_, arena_) };
        h.radius = rng.range(arena_ * 0.20f, arena_ * 0.55f);
        h.height = rng.range(1.5f, 5.0f) * (0.4f + cfg.hilliness);
        hills_.push_back(h);
    }

    // mountains (taller, broader peaks)
    if (cfg.mountainous) {
        const int peaks = 1 + static_cast<int>(rng.unit() * 2.0f);
        for (int i = 0; i < peaks; ++i) {
            Hill m;
            m.center = { rng.range(-arena_ * 0.7f, arena_ * 0.7f),
                         rng.range(-arena_ * 0.7f, arena_ * 0.7f) };
            m.radius = rng.range(arena_ * 0.45f, arena_ * 0.7f);
            m.height = rng.range(12.0f, 20.0f);
            hills_.push_back(m);
        }
    }

    // river (one straight channel across the field)
    if (hasRiver_) {
        const float ang = rng.range(0.0f, PI);
        const Vector2 dir  = { cosf(ang), sinf(ang) };
        const Vector2 perp = { -dir.y, dir.x };
        const float off = rng.range(-arena_ * 0.3f, arena_ * 0.3f);
        const Vector2 mid = Vector2Scale(perp, off);
        riverA_ = Vector2Subtract(mid, Vector2Scale(dir, arena_ * 2.0f));
        riverB_ = Vector2Add(mid, Vector2Scale(dir, arena_ * 2.0f));
        riverHalfWidth_ = rng.range(3.0f, 6.0f);
    }

    // trees (scatter on walkable ground)
    const int treeCount = static_cast<int>(cfg.forestDensity * 60.0f);
    for (int i = 0; i < treeCount; ++i) {
        const float tx = rng.range(-arena_, arena_);
        const float tz = rng.range(-arena_, arena_);
        if (IsWaterAt(tx, tz)) continue;
        const float h = HeightAt(tx, tz);
        if (h > 9.0f) continue;   // no trees on bare peaks
        const float dhx = HeightAt(tx + 1.0f, tz) - h;
        const float dhz = HeightAt(tx, tz + 1.0f) - h;
        if (fabsf(dhx) > 1.2f || fabsf(dhz) > 1.2f) continue;   // too steep

        Tree t;
        t.pos    = { tx, h, tz };
        t.height = rng.range(3.0f, 6.0f);
        t.radius = rng.range(0.6f, 1.2f);
        const float g = rng.range(0.55f, 0.9f);
        t.foliage = Color{ static_cast<unsigned char>(40 * g),
                           static_cast<unsigned char>(110 * g),
                           static_cast<unsigned char>(50 * g), 255 };
        trees_.push_back(t);
    }

    // precompute the render grid by sampling the height function once
    gridN_ = 48;
    cell_  = (2.0f * arena_) / gridN_;
    gridH_.resize((gridN_ + 1) * (gridN_ + 1));
    gridWater_.resize(gridH_.size());
    for (int j = 0; j <= gridN_; ++j) {
        for (int i = 0; i <= gridN_; ++i) {
            const float x = -arena_ + i * cell_;
            const float z = -arena_ + j * cell_;
            gridH_[gridIndex(i, j)]     = HeightAt(x, z);
            gridWater_[gridIndex(i, j)] = IsWaterAt(x, z) ? 1 : 0;
        }
    }
}

float Terrain::RawHeight(float x, float z) const {
    float h = 0.0f;
    for (const Hill& hill : hills_) {
        const float d = Vector2Distance({ x, z }, hill.center);
        if (d < hill.radius)
            h += hill.height * SmoothStep(1.0f - d / hill.radius);
    }
    return h;
}

float Terrain::RiverDistance(float x, float z) const {
    const Vector2 p = { x, z };
    const Vector2 ab = Vector2Subtract(riverB_, riverA_);
    const float denom = Vector2DotProduct(ab, ab);
    if (denom < 1e-6f) return 1e9f;
    const float t = Vector2DotProduct(Vector2Subtract(p, riverA_), ab) / denom;
    const Vector2 proj = Vector2Add(riverA_, Vector2Scale(ab, t));
    return Vector2Distance(p, proj);
}

float Terrain::HeightAt(float x, float z) const {
    float h = RawHeight(x, z);
    if (hasRiver_) {
        const float d = RiverDistance(x, z);
        if (d < riverHalfWidth_) {
            const float bed = waterLevel_ - 1.5f;   // carve toward a riverbed
            const float carved = Lerp(bed, h, SmoothStep(d / riverHalfWidth_));
            h = fminf(h, carved);
        }
    }
    return h;
}

bool Terrain::IsWaterAt(float x, float z) const {
    return hasRiver_ && RiverDistance(x, z) < riverHalfWidth_;
}

// Build the whole surface as ONE mesh (flat-shaded: unshared vertices with
// per-triangle colors), upload it once, and draw it as a model from then on.
void Terrain::BakeModel() const {
    Mesh mesh{};
    mesh.triangleCount = gridN_ * gridN_ * 2;
    mesh.vertexCount   = mesh.triangleCount * 3;
    mesh.vertices = (float*)MemAlloc((unsigned)mesh.vertexCount * 3 * sizeof(float));
    mesh.normals  = (float*)MemAlloc((unsigned)mesh.vertexCount * 3 * sizeof(float));
    mesh.colors   = (unsigned char*)MemAlloc((unsigned)mesh.vertexCount * 4);

    int v = 0;
    auto emit = [&](Vector3 p, Color col) {
        mesh.vertices[v * 3 + 0] = p.x;
        mesh.vertices[v * 3 + 1] = p.y;
        mesh.vertices[v * 3 + 2] = p.z;
        mesh.normals[v * 3 + 0] = 0;
        mesh.normals[v * 3 + 1] = 1;
        mesh.normals[v * 3 + 2] = 0;
        mesh.colors[v * 4 + 0] = col.r;
        mesh.colors[v * 4 + 1] = col.g;
        mesh.colors[v * 4 + 2] = col.b;
        mesh.colors[v * 4 + 3] = col.a;
        ++v;
    };
    for (int j = 0; j < gridN_; ++j) {
        for (int i = 0; i < gridN_; ++i) {
            const float x0 = -arena_ + i * cell_;
            const float x1 = x0 + cell_;
            const float z0 = -arena_ + j * cell_;
            const float z1 = z0 + cell_;
            const Vector3 A = { x0, gridH_[gridIndex(i,     j)],     z0 };
            const Vector3 Bv= { x1, gridH_[gridIndex(i + 1, j)],     z0 };
            const Vector3 C = { x0, gridH_[gridIndex(i,     j + 1)], z1 };
            const Vector3 D = { x1, gridH_[gridIndex(i + 1, j + 1)], z1 };
            // Winding A,C,B / B,C,D gives an upward-facing (+y) normal.
            const Color c1 = TriangleColor(A, C, Bv);
            const Color c2 = TriangleColor(Bv, C, D);
            emit(A, c1); emit(C, c1); emit(Bv, c1);
            emit(Bv, c2); emit(C, c2); emit(D, c2);
        }
    }
    UploadMesh(&mesh, false);
    model_ = LoadModelFromMesh(mesh);
    baked_ = true;
}

void Terrain::Draw() const {
    if (!baked_) BakeModel();
    DrawModel(model_, { 0, 0, 0 }, 1.0f, WHITE);

    // water surface (flat translucent quads over river cells)
    if (hasRiver_) {
        const Color water = Color{ 46, 98, 150, 165 };
        for (int j = 0; j < gridN_; ++j) {
            for (int i = 0; i < gridN_; ++i) {
                const float cx = -arena_ + (i + 0.5f) * cell_;
                const float cz = -arena_ + (j + 0.5f) * cell_;
                if (!IsWaterAt(cx, cz)) continue;
                DrawPlane({ cx, waterLevel_ + 0.06f, cz }, { cell_, cell_ }, water);
            }
        }
    }

    // trees
    for (const Tree& t : trees_) {
        const Vector3 base     = t.pos;
        const Vector3 trunkTop = { base.x, base.y + t.height * 0.4f, base.z };
        const Vector3 crownMid = { base.x, base.y + t.height * 0.3f, base.z };
        const Vector3 crownTop = { base.x, base.y + t.height,        base.z };
        DrawCylinderEx(base, trunkTop, t.radius * 0.25f, t.radius * 0.2f, 6,
                       Color{ 92, 64, 40, 255 });
        DrawCylinderEx(crownMid, crownTop, t.radius, 0.0f, 8, t.foliage);
    }
}

// ===========================================================================
// Formations — the player commands their own troops from the strategy menu (~).
// Charge = attack the nearest foe (classic melee). Line/Square/Spread march the
// troops into shaped slots around an anchor (the player) and hold there, still
// fighting anything that reaches them. `ranks` sets how many rows a line forms.
// Add a new shape by extending FormationType + FormationTarget; nothing else
// needs to change. (An allied party fights on its own — it always charges.)
// ===========================================================================
enum class FormationType { Charge, Line, Square, Spread };

const char* FormationName(FormationType f) {
    switch (f) {
        case FormationType::Charge: return "Charge";
        case FormationType::Line:   return "Line";
        case FormationType::Square: return "Square";
        case FormationType::Spread: return "Spread";
    }
    return "?";
}

constexpr float FORM_SPACING = 2.4f;   // gap between troops in a formation
constexpr float FORM_FRONT   = 4.0f;   // distance the front rank forms ahead of the anchor

// World-space slot for own-troop `slot` of `count`, around a formation anchor
// (position + facing yaw). Charge is handled by the AI directly, not here.
Vector3 FormationTarget(FormationType type, int ranks, Vector3 anchor, float yaw,
                        int slot, int count) {
    const Vector3 fwd   = { sinf(yaw), 0, cosf(yaw) };
    // Same screen-right convention as player strafing: cross(fwd, up).
    const Vector3 right = { -fwd.z, 0, fwd.x };
    auto place = [&](float rx, float fz) {
        return Vector3{ anchor.x + right.x * rx + fwd.x * fz, anchor.y,
                        anchor.z + right.z * rx + fwd.z * fz };
    };

    if (type == FormationType::Square) {
        const int   per  = count > 0 ? count : 1;
        const float side = FORM_SPACING * fmaxf(1.0f, ceilf(per / 4.0f));
        const float half = side * 0.5f;
        const float d    = (static_cast<float>(slot) / per) * 4.0f * side;   // around perimeter
        float rx, fz;
        if      (d < side)     { rx = -half + d;              fz =  half; }
        else if (d < 2 * side) { rx =  half;                  fz =  half - (d - side); }
        else if (d < 3 * side) { rx =  half - (d - 2 * side); fz = -half; }
        else                   { rx = -half;                  fz = -half + (d - 3 * side); }
        return place(rx, fz + FORM_FRONT);
    }

    // Line / Spread: `ranks` rows, filled left-to-right, front rank ahead.
    const int   r    = ranks > 0 ? ranks : 1;
    const int   cols = (count + r - 1) / r;                         // ceil
    const int   row  = (cols > 0) ? slot / cols : 0;
    const int   col  = (cols > 0) ? slot % cols : 0;
    const float sp   = (type == FormationType::Spread) ? FORM_SPACING * 2.2f : FORM_SPACING;
    const float rx   = (col - (cols - 1) * 0.5f) * sp;
    const float fz   = FORM_FRONT - row * FORM_SPACING;
    return place(rx, fz);
}

// Choose which carried weapon to use at an engagement distance: the shortest
// weapon that still reaches, otherwise the longest available. A unit with a
// spear and a sword thus uses the spear at range and the sword up close.
int PickWeaponForRange(const Content& c, const Loadout& lo, float dist) {
    const int n = lo.weaponCount();
    if (n <= 0) return -1;
    int   shortestSufficient = -1;  float ssReach = 1e9f;
    int   longest = lo.weaponAt(0); float longReach = -1.0f;
    for (int i = 0; i < n; ++i) {
        const int h = lo.weaponAt(i);
        if (!c.weapons.valid(h)) continue;
        const WeaponDef& w = c.weapons[h];
        // A bow's effective "reach" is its missile range, so an archer keeps the
        // bow up until a foe is inside melee reach of its sidearm.
        const float r = w.isRanged() ? w.missileRange
                                     : (w.reach > 0.5f ? w.reach : FIST_REACH);
        if (r >= dist && r < ssReach) { ssReach = r; shortestSufficient = h; }
        if (r > longReach)            { longReach = r; longest = h; }
    }
    return shortestSufficient >= 0 ? shortestSufficient : longest;
}

// One soldier's decided actions for a frame, computed in parallel then applied
// serially so there are no data races.
struct AICmd {
    float   yaw = 0;
    Vector3 step{};          // xz movement this frame
    Vector3 separation{};    // push away from crowding neighbours
    float   walkAdd = 0;
    float   newCooldown = 0;
    float   newSwing = 0;
    int     newTarget = -1;
    int     activeWeapon = -1;
    int     hitSoldier = -1; // soldier index to damage this frame, or -1
    bool    hitPlayer = false;
    float   hitDamage = 0;
    bool    shoot = false;   // loose a missile instead of a melee blow
    Vector3 shootAt{};       // where the missile is aimed
};

// A missile in flight. Team says who it can hurt (no friendly fire — allied
// archers shooting over a melee would otherwise slaughter their own side).
struct Arrow {
    Vector3 pos;
    Vector3 vel;
    Team    team;
    float   damage = 0;
    float   life = ARROW_LIFETIME;
    bool    alive = true;
};

struct Soldier {
    Vector3 pos;
    int     troop;        // troop handle
    Team    team;
    bool    ally = false; // Team::Player, but a friendly party's troop, not yours
    float   hp;
    float   maxHp;
    float   yaw = 0;
    float   cooldown = 0;
    float   swing = 0;
    float   flash = 0;         // just-hit white flare, decays fast
    float   walkPhase = 0;
    int     target = -1;
    int     slot = -1;         // formation slot (player's own troops only)
    int     activeWeapon = -1; // currently-wielded weapon handle
    bool    onWall = false;    // garrison archer posted on the siege wall
    float   trampleCd = 0;     // mounted: cooldown between trample hits
};

// Cavalry trample. TODO(balance): damage/cooldown; structure only.
constexpr float TRAMPLE_DAMAGE   = 15.0f;
constexpr float TRAMPLE_COOLDOWN = 1.5f;
constexpr float TRAMPLE_RADIUS   = 1.3f;

struct BattleState {
    BattleSetup          setup;     // world-map snapshot this battle runs on
    Terrain              terrain;   // generated from setup.campaignPos
    bool                 hasWall = false;   // siege of a walled settlement
    std::vector<Soldier> soldiers;
    std::vector<Arrow>   arrows;    // missiles in flight

    // Player avatar
    Vector3 pPos;
    float   pHp = 0, pMaxHp = 0;
    float   yaw = 0, pitch = 0;
    float   cooldown = 0;
    float   swing = 0;
    AttackDir attackDir = AttackDir::Right;
    bool    blocking = false;
    float   vY = 0;
    float   walkPhase = 0;
    float   pFlash = 0;         // hero just-hit feedback

    bool  over = false;
    bool  won = false;
    float overTimer = 0;
    bool  reported = false;         // outcome handed back to the caller
    int   aliveAllies = 0, aliveEnemies = 0;   // tallied each update, drawn in HUD

    // Manual mouse tracking (GetMouseDelta unreliable under WSL/X11).
    Vector2 lastMouse{ 0, 0 };
    bool    hasLastMouse = false;
    Vector2 aimAccum{ 0, 0 };       // recent mouse motion, for attack direction

    // Combat: hold LMB to ready a swing, release to strike.
    bool  readying = false;
    float windup = 0.0f;

    // Hero arsenal (a hero may carry several weapons and switch between them).
    std::vector<int> heroArsenal;
    int  heroWeapon = 0;

    // Formations / strategy menu.
    FormationType formation = FormationType::Charge;
    int   ranks = 3;
    bool  showMenu = false;
    int   ownCount = 0;             // player's own (non-ally) troop count

    // Parallel-AI scratch, reused each frame to avoid per-frame allocation.
    std::vector<AICmd> cmds;
    std::vector<float> dmg;
};

BattleState B;

// Pick an attack direction from accumulated mouse motion.
AttackDir DirFromMotion(Vector2 m) {
    if (fabsf(m.x) > fabsf(m.y))
        return m.x > 0 ? AttackDir::Right : AttackDir::Left;
    return m.y < 0 ? AttackDir::Up : AttackDir::Down;
}

const Loadout& TroopLoadout(const Content& c, int troop) {
    return c.troops[troop].loadout;
}

// Combat stats come from the wielded weapon (WeaponDef), with bare-hand
// fallbacks — this is the single place battle numbers are read from content.
float WeaponDamage(const Content& c, int wh) {
    return c.weapons.valid(wh) && c.weapons[wh].damage > 0.0f ? c.weapons[wh].damage : FIST_DAMAGE;
}
float WeaponReach(const Content& c, int wh) {
    return c.weapons.valid(wh) && c.weapons[wh].reach > 0.5f ? c.weapons[wh].reach : FIST_REACH;
}
float WeaponCooldown(const Content& c, int wh) {
    return c.weapons.valid(wh) && c.weapons[wh].swingTime > 0.05f ? c.weapons[wh].swingTime : FIST_SWING;
}

// Worn armour soaks a flat amount per hit; a landed blow always tells a little.
// TODO(balance): the soak curve is placeholder-simple.
float ApplyArmor(float damage, int armor) {
    return fmaxf(damage - (float)armor, 1.0f);
}

void SpawnLine(const Content& c, Team team, const std::vector<int>& counts, float zBase,
               bool ally = false) {
    int n = 0;
    for (int troop = (int)counts.size() - 1; troop >= 0; --troop) {
        for (int i = 0; i < counts[troop]; ++i) {
            Soldier s;
            s.troop = troop;
            s.team = team;
            s.ally = ally;
            s.maxHp = (float)c.troops[troop].maxHp;
            s.hp = s.maxHp;
            s.slot = (team == Team::Player && !ally) ? n : -1;   // formation slot
            s.activeWeapon = c.troops[troop].loadout.weaponAt(0);
            const float x = -20.0f + (n % 10) * 4.0f;
            const float z = zBase + (n / 10) * 4.0f * (team == Team::Enemy ? 1.0f : -1.0f);
            s.pos = { x, B.terrain.HeightAt(x, z), z };
            s.yaw = (team == Team::Enemy) ? PI : 0.0f;
            B.soldiers.push_back(s);
            ++n;
        }
    }
}

bool HasRangedWeapon(const Content& c, const Loadout& lo) {
    for (int i = 0; i < lo.weaponCount(); ++i)
        if (c.weapons.valid(lo.weaponAt(i)) && c.weapons[lo.weaponAt(i)].isRanged())
            return true;
    return false;
}

// Siege defenders: archers take posts along the wall top; melee musters in
// the yard behind the gate, ready to plug the breach.
void SpawnGarrison(const Content& c, const std::vector<int>& counts) {
    int wallN = 0, yardN = 0;
    for (int troop = (int)counts.size() - 1; troop >= 0; --troop) {
        for (int i = 0; i < counts[troop]; ++i) {
            Soldier s;
            s.troop = troop;
            s.team = Team::Enemy;
            s.maxHp = (float)c.troops[troop].maxHp;
            s.hp = s.maxHp;
            s.activeWeapon = c.troops[troop].loadout.weaponAt(0);
            s.yaw = PI;   // face the attackers
            if (HasRangedWeapon(c, c.troops[troop].loadout)) {
                const float x = (wallN % 2 ? 1.0f : -1.0f) *
                                (GATE_HALF + 4.0f + 3.0f * (float)(wallN / 2));
                s.pos = { x, B.terrain.HeightAt(x, WALL_Z) + WALL_HEIGHT, WALL_Z };
                s.onWall = true;
                ++wallN;
            } else {
                const float x = -12.0f + (yardN % 8) * 3.2f;
                const float z = WALL_Z + 6.0f + (float)(yardN / 8) * 3.0f;
                s.pos = { x, B.terrain.HeightAt(x, z), z };
                ++yardN;
            }
            B.soldiers.push_back(s);
        }
    }
}

int FindNearest(const Soldier& me, Team wantTeam) {
    int best = -1;
    float bestD = 1e9f;
    for (int i = 0; i < (int)B.soldiers.size(); ++i) {
        const Soldier& o = B.soldiers[i];
        if (o.hp <= 0 || o.team != wantTeam) continue;
        const float d = Vector3DistanceSqr(me.pos, o.pos);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

// Siege-wall helpers (defined below, near the other world-collision code).
void    EnforceWall(Vector3& p);
Vector3 FunnelThroughGate(Vector3 pos, Vector3 goal);

// Decide one soldier's actions for this frame from a READ-ONLY view of the
// world (no soldier is mutated). Safe to run for many soldiers concurrently;
// results are applied serially by the caller. Player-owned troops obey the
// current formation (unless a foe is already on them); allies and enemies
// charge.
AICmd ComputeAI(const Content& c, int i, float dt, FormationType formation,
                int ranks, Vector3 anchor, float anchorYaw, int ownCount) {
    const Soldier& s = B.soldiers[i];
    AICmd cmd;
    cmd.yaw         = s.yaw;
    cmd.newCooldown = s.cooldown - dt;
    cmd.newSwing    = s.swing > 0 ? s.swing - dt * 4.0f : 0.0f;
    cmd.newTarget   = s.target;

    const Team foe = (s.team == Team::Player) ? Team::Enemy : Team::Player;
    int target = s.target;
    if (target < 0 || target >= (int)B.soldiers.size() ||
        B.soldiers[target].hp <= 0 || B.soldiers[target].team == s.team) {
        target = FindNearest(s, foe);
    }
    cmd.newTarget = target;

    // Enemies may prefer the player if closer than their nearest soldier foe.
    bool    targetPlayer = false;
    bool    haveFoe = false;
    Vector3 tpos{};
    if (s.team == Team::Enemy) {
        const float dp = Vector3DistanceSqr(s.pos, B.pPos);
        if (target < 0 || dp < Vector3DistanceSqr(s.pos, B.soldiers[target].pos)) {
            targetPlayer = true; tpos = B.pPos; haveFoe = true;
        }
    }
    if (!targetPlayer && target >= 0) { tpos = B.soldiers[target].pos; haveFoe = true; }

    float foeDist = 1e9f;
    if (haveFoe) { Vector3 t = Vector3Subtract(tpos, s.pos); t.y = 0; foeDist = Vector3Length(t); }

    // Pick the best carried weapon for this distance (multi-weapon support).
    const Loadout& lo = TroopLoadout(c, s.troop);
    cmd.activeWeapon = PickWeaponForRange(c, lo, haveFoe ? foeDist : FIST_REACH);
    const WeaponDef* wd = c.weapons.valid(cmd.activeWeapon) ? &c.weapons[cmd.activeWeapon] : nullptr;
    const bool ranged = wd && wd->isRanged();
    // Archers advance until comfortably inside missile range, then stand and
    // loose; melee closes to arm's reach.
    const float engage = ranged ? wd->missileRange * 0.9f
                                : WeaponReach(c, cmd.activeWeapon) * 0.8f;

    const bool commanded =
        (s.team == Team::Player && !s.ally && formation != FormationType::Charge);

    Vector3 goal;
    bool holding = false;
    if (commanded && !(haveFoe && foeDist <= engage * 1.3f)) {
        goal = FormationTarget(formation, ranks, anchor, anchorYaw, s.slot, ownCount);
        holding = true;
    } else if (haveFoe) {
        goal = tpos;
    } else {
        goal = s.pos;
    }

    goal = FunnelThroughGate(s.pos, goal);   // siege walls funnel everyone
    Vector3 to = Vector3Subtract(goal, s.pos);
    to.y = 0;
    const float dist = Vector3Length(to);
    if (dist > 0.01f) cmd.yaw = atan2f(to.x, to.z);

    const bool foeInReach = haveFoe && !holding && foeDist <= engage;
    if (foeInReach && cmd.newCooldown <= 0.0f) {
        cmd.newCooldown = WeaponCooldown(c, cmd.activeWeapon);
        cmd.newSwing    = 1.0f;
        cmd.hitDamage   = WeaponDamage(c, cmd.activeWeapon);
        if (ranged)           { cmd.shoot = true; cmd.shootAt = tpos; }
        else if (targetPlayer) cmd.hitPlayer = true;
        else                   cmd.hitSoldier = target;
    } else if (dist > (holding ? 0.4f : engage)) {
        cmd.step    = Vector3Scale(Vector3Normalize(to), c.troops[s.troop].moveSpeed * dt);
        cmd.walkAdd = dt * 10.0f;
    }

    // Separation: sum pushes from crowding neighbours (read-only).
    Vector3 sep{ 0, 0, 0 };
    const int nn = (int)B.soldiers.size();
    for (int j = 0; j < nn; ++j) {
        if (j == i) continue;
        const Soldier& o = B.soldiers[j];
        if (o.hp <= 0) continue;
        Vector3 d = Vector3Subtract(s.pos, o.pos);
        d.y = 0;
        const float len = Vector3Length(d);
        if (len < SOLDIER_RADIUS * 2 && len > 0.001f)
            sep = Vector3Add(sep, Vector3Scale(Vector3Normalize(d),
                                               (SOLDIER_RADIUS * 2 - len) * 0.5f));
    }
    cmd.separation = sep;

    // Wall posts are held to the death: no advancing, no crowd-shuffling.
    if (s.onWall) {
        cmd.step = { 0, 0, 0 };
        cmd.separation = { 0, 0, 0 };
        cmd.walkAdd = 0;
    }
    return cmd;
}

Color TeamTint(Team t) { return t == Team::Enemy ? RED : BLUE; }

// Keep a mover out of the wall band unless it's inside the gate opening.
void EnforceWall(Vector3& p) {
    if (!B.hasWall) return;
    if (fabsf(p.x) <= GATE_HALF) return;                 // in the gateway
    const float dz = p.z - WALL_Z;
    if (fabsf(dz) >= WALL_BAND) return;
    p.z = WALL_Z + (dz < 0 ? -WALL_BAND : WALL_BAND);    // push back to own side
}

// If the straight path to `goal` crosses the wall outside the gate, steer via
// the gate mouth on the mover's own side first.
Vector3 FunnelThroughGate(Vector3 pos, Vector3 goal) {
    if (!B.hasWall) return goal;
    const bool crossing = (pos.z - WALL_Z) * (goal.z - WALL_Z) < 0;
    if (!crossing || fabsf(pos.x) <= GATE_HALF * 0.8f) return goal;
    const float side = pos.z < WALL_Z ? -1.0f : 1.0f;
    return { 0, pos.y, WALL_Z + side * 2.5f };           // gate mouth, own side
}

void EndBattle(bool won) {
    B.over = true;
    B.won = won;
    B.overTimer = 2.5f;
    if (IsWindowReady()) EnableCursor();   // headless harness has no window
}

// Losses = starting counts minus living soldiers of that contingent, per troop.
// The player's own troops and an allied party's troops both fight on
// Team::Player but are tracked separately via Soldier::ally.
std::vector<int> ComputeLosses() {
    std::vector<int> losses = B.setup.playerTroops;
    for (const Soldier& s : B.soldiers)
        if (s.team == Team::Player && !s.ally && s.hp > 0) losses[s.troop]--;
    return losses;
}

std::vector<int> ComputeAllyLosses() {
    std::vector<int> losses = B.setup.allyTroops;   // empty when no ally joined
    if (losses.empty()) return losses;
    for (const Soldier& s : B.soldiers)
        if (s.team == Team::Player && s.ally && s.hp > 0) losses[s.troop]--;
    return losses;
}

std::vector<int> ComputeEnemyLosses() {
    std::vector<int> losses = B.setup.enemyTroops;
    for (const Soldier& s : B.soldiers)
        if (s.team == Team::Enemy && s.hp > 0) losses[s.troop]--;
    return losses;
}

}  // namespace

void BattleInit(const Content& c, const BattleSetup& setup) {
    B = BattleState{};
    B.setup = setup;
    B.terrain.Generate(TerrainConfigFromWorld(setup.campaignPos), ARENA);
    B.hasWall = setup.siege && setup.siegeType != SettlementType::Village;

    SpawnLine(c, Team::Player, setup.playerTroops, -30.0f);
    if (B.hasWall) SpawnGarrison(c, setup.enemyTroops);   // walls + yard posts
    else           SpawnLine(c, Team::Enemy, setup.enemyTroops, 30.0f);
    // An allied party, if one joined the fight, forms up just behind your line.
    if (!setup.allyTroops.empty())
        SpawnLine(c, Team::Player, setup.allyTroops, -48.0f, /*ally=*/true);

    // Count the player's own troops (formation slots were assigned in SpawnLine)
    // and set up the hero's carried weapons.
    B.ownCount = 0;
    for (const Soldier& s : B.soldiers)
        if (s.team == Team::Player && !s.ally) ++B.ownCount;

    B.heroArsenal.clear();
    for (int i = 0; i < setup.heroLoadout.weaponCount(); ++i)
        B.heroArsenal.push_back(setup.heroLoadout.weaponAt(i));
    if (B.heroArsenal.empty()) B.heroArsenal.push_back(-1);
    B.heroWeapon = 0;
    B.setup.heroLoadout.set(EquipSlot::Weapon, B.heroArsenal[0]);

    B.pPos = { 0, B.terrain.HeightAt(0.0f, -38.0f), -38 };
    B.pMaxHp = (float)setup.heroMaxHp;
    B.pHp = B.pMaxHp;

    if (IsWindowReady()) DisableCursor();   // headless harness has no window
    B.hasLastMouse = false;
}

// Read the real devices into battle intent. Windowed play only — the headless
// harness builds BattleInput directly. Mouse-look uses manual position deltas
// (GetMouseDelta is unreliable under WSL/X11).
BattleInput GatherBattleInput() {
    BattleInput in;

    Vector2 md = { 0, 0 };
    const Vector2 mouse = GetMousePosition();
    if (B.hasLastMouse) {
        md = Vector2Subtract(mouse, B.lastMouse);
        if (Vector2Length(md) > 80.0f) md = { 0, 0 };   // window-focus jump guard
    }
    B.lastMouse = mouse;
    B.hasLastMouse = true;
    in.lookDelta = md;

    if (IsKeyDown(KEY_W)) in.moveForward += 1;
    if (IsKeyDown(KEY_S)) in.moveForward -= 1;
    if (IsKeyDown(KEY_D)) in.moveRight   += 1;
    if (IsKeyDown(KEY_A)) in.moveRight   -= 1;
    in.jump          = IsKeyPressed(KEY_SPACE);
    in.block         = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    in.attackPress   = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    in.attackRelease = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    in.swapWeapon    = IsKeyPressed(KEY_Q);
    in.toggleMenu    = IsKeyPressed(KEY_GRAVE);
    if (IsKeyPressed(KEY_ONE))   in.formationSelect = 1;
    if (IsKeyPressed(KEY_TWO))   in.formationSelect = 2;
    if (IsKeyPressed(KEY_THREE)) in.formationSelect = 3;
    if (IsKeyPressed(KEY_FOUR))  in.formationSelect = 4;
    if (IsKeyPressed(KEY_LEFT_BRACKET))  in.ranksDelta -= 1;
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) in.ranksDelta += 1;
    return in;
}

bool BattleUpdate(const Content& c, float dt, const BattleInput& in, BattleOutcome& out) {
    if (dt > 0.05f) dt = 0.05f;

    // ---------- player intent ----------
    Vector3 fwd = { sinf(B.yaw), 0, cosf(B.yaw) };
    if (!B.over) {
        const Vector2 md = in.lookDelta;
        B.yaw   -= md.x * 0.003f;
        B.pitch = Clamp(B.pitch - md.y * 0.003f, -0.4f, 0.6f);
        fwd = Vector3{ sinf(B.yaw), 0, cosf(B.yaw) };
        // Screen-right = cross(fwd, up) in raylib's right-handed frame.
        const Vector3 right = { -fwd.z, 0, fwd.x };

        // Track recent motion (decaying) so an attack reads the latest flick.
        B.aimAccum = Vector2Add(Vector2Scale(B.aimAccum, 0.6f), md);

        Vector3 move = Vector3Add(Vector3Scale(fwd, in.moveForward),
                                  Vector3Scale(right, in.moveRight));
        if (Vector3Length(move) > 0) {
            const float speed = B.blocking ? 3.0f : 7.0f;
            B.pPos = Vector3Add(B.pPos, Vector3Scale(Vector3Normalize(move), speed * dt));
            B.walkPhase += dt * 10.0f;
        }
        B.pPos.x = Clamp(B.pPos.x, -ARENA, ARENA);
        B.pPos.z = Clamp(B.pPos.z, -ARENA, ARENA);
        EnforceWall(B.pPos);

        const float groundY = B.terrain.HeightAt(B.pPos.x, B.pPos.z);
        if (in.jump && B.pPos.y <= groundY + 0.02f) B.vY = 6.0f;
        B.vY -= 18.0f * dt;
        B.pPos.y += B.vY * dt;
        if (B.pPos.y < groundY) { B.pPos.y = groundY; B.vY = 0; }

        B.blocking = in.block;
        B.cooldown -= dt;
        if (B.swing > 0) B.swing -= dt * 4.0f;

        // ---- switch active weapon (a hero may carry several) ----
        if (in.swapWeapon && (int)B.heroArsenal.size() > 1) {
            B.heroWeapon = (B.heroWeapon + 1) % (int)B.heroArsenal.size();
            B.setup.heroLoadout.set(EquipSlot::Weapon, B.heroArsenal[B.heroWeapon]);
        }

        // ---- strategy / formation menu (~ toggles it) ----
        if (in.toggleMenu) B.showMenu = !B.showMenu;
        if (B.showMenu) {
            switch (in.formationSelect) {
                case 1: B.formation = FormationType::Charge; break;
                case 2: B.formation = FormationType::Line;   break;
                case 3: B.formation = FormationType::Square; break;
                case 4: B.formation = FormationType::Spread; break;
                default: break;
            }
            B.ranks += in.ranksDelta;
            if (B.ranks < 1) B.ranks = 1;
            if (B.ranks > 8) B.ranks = 8;
        }

        // ---- attack: HOLD LMB to ready a swing in the direction you move the
        //      mouse, then RELEASE to strike (Mount & Blade style) ----
        if (B.blocking) { B.readying = false; B.windup = 0.0f; }   // guarding cancels a wind-up
        if (in.attackPress && B.cooldown <= 0 && !B.blocking) {
            B.readying = true;
            B.windup = 0.0f;
        }
        if (B.readying) {
            B.windup = fminf(1.0f, B.windup + dt * 4.0f);
            if (Vector2Length(B.aimAccum) > 3.0f)          // re-aim while holding
                B.attackDir = DirFromMotion(B.aimAccum);
        }
        if (in.attackRelease && B.readying) {
            B.readying = false;
            B.windup = 0.0f;
            B.swing = 1.0f;
            const int wh = B.setup.heroLoadout.get(EquipSlot::Weapon);
            const float reach = WeaponReach(c, wh);
            B.cooldown = WeaponCooldown(c, wh);
            for (Soldier& s : B.soldiers) {
                if (s.hp <= 0 || s.team != Team::Enemy) continue;
                Vector3 to = Vector3Subtract(s.pos, B.pPos);
                to.y = 0;
                const float d = Vector3Length(to);
                if (d < reach + 0.6f && d > 0.01f &&
                    Vector3DotProduct(Vector3Normalize(to), fwd) > 0.4f) {
                    // ~120° frontal arc; the target's armour soaks per hit.
                    s.hp -= ApplyArmor(WeaponDamage(c, wh),
                                       LoadoutArmor(c, TroopLoadout(c, s.troop)));
                }
            }
        }
    } else {
        B.overTimer -= dt;
        if (B.overTimer <= 0 && !B.reported) {
            B.reported = true;
            out.won = B.won;
            out.playerLosses = ComputeLosses();
            out.allyLosses   = ComputeAllyLosses();
            out.enemyLosses  = ComputeEnemyLosses();
            return false;   // battle over — caller returns to the world map
        }
    }

    // ---------- soldier AI (multithreaded) ----------
    const Vector3 anchor    = B.pPos;
    const float   anchorYaw = B.yaw;
    const int     n = (int)B.soldiers.size();
    if (!B.over && n > 0) {
        B.cmds.resize(n);
        // Phase 1 — compute every soldier's intent in parallel from a read-only
        // snapshot. Nothing is mutated here, so there are no data races.
        ThreadPool::Global().For(0, n, 24, [&](int i) {
            if (B.soldiers[i].hp > 0)
                B.cmds[i] = ComputeAI(c, i, dt, B.formation, B.ranks, anchor, anchorYaw, B.ownCount);
        });
        // Phase 2 — apply movement/state serially, accumulate damage, then deal it.
        B.dmg.assign(n, 0.0f);
        float playerDamage = 0.0f;
        for (int i = 0; i < n; ++i) {
            Soldier& s = B.soldiers[i];
            if (s.hp <= 0) continue;
            const AICmd& cmd = B.cmds[i];
            s.yaw          = cmd.yaw;
            s.cooldown     = cmd.newCooldown;
            s.swing        = cmd.newSwing;
            s.target       = cmd.newTarget;
            s.activeWeapon = cmd.activeWeapon;
            s.walkPhase   += cmd.walkAdd;
            s.pos = Vector3Add(s.pos, Vector3Add(cmd.step, cmd.separation));
            EnforceWall(s.pos);

            // Cavalry at the gallop tramples whoever it rides through.
            s.trampleCd -= dt;
            if (c.troops[s.troop].mounted && s.trampleCd <= 0 &&
                Vector3Length(cmd.step) > 0.5f * c.troops[s.troop].moveSpeed * dt) {
                for (int j = 0; j < n; ++j) {
                    Soldier& o = B.soldiers[j];
                    if (o.hp <= 0 || o.team == s.team || o.onWall) continue;
                    Vector3 d3 = Vector3Subtract(o.pos, s.pos);
                    d3.y = 0;
                    if (Vector3LengthSqr(d3) < TRAMPLE_RADIUS * TRAMPLE_RADIUS) {
                        B.dmg[j] += ApplyArmor(TRAMPLE_DAMAGE,
                                               LoadoutArmor(c, TroopLoadout(c, o.troop)));
                        s.trampleCd = TRAMPLE_COOLDOWN;
                        break;
                    }
                }
            }
            // Armour soaks per hit, so reduction happens per blow, not per frame.
            if (cmd.hitSoldier >= 0)
                B.dmg[cmd.hitSoldier] += ApplyArmor(
                    cmd.hitDamage,
                    LoadoutArmor(c, TroopLoadout(c, B.soldiers[cmd.hitSoldier].troop)));
            if (cmd.hitPlayer)
                playerDamage += ApplyArmor(cmd.hitDamage,
                                           LoadoutArmor(c, B.setup.heroLoadout));
            if (cmd.shoot) {
                // Loose an arrow from chest height, lobbed to compensate for
                // gravity drop over the flight time (simple ballistic aim).
                Arrow a;
                a.team   = s.team;
                a.damage = cmd.hitDamage;
                a.pos    = Vector3Add(s.pos, { 0, 1.5f, 0 });
                const Vector3 target = Vector3Add(cmd.shootAt, { 0, 1.2f, 0 });
                Vector3 delta = Vector3Subtract(target, a.pos);
                const float distTo = Vector3Length(delta);
                const bool  validSpeed = c.weapons.valid(cmd.activeWeapon) &&
                                         c.weapons[cmd.activeWeapon].missileSpeed > 1.0f;
                const float speed = validSpeed ? c.weapons[cmd.activeWeapon].missileSpeed : 30.0f;
                a.vel = Vector3Scale(Vector3Normalize(delta), speed);
                a.vel.y += 0.5f * ARROW_GRAVITY * (distTo / speed);
                B.arrows.push_back(a);
            }
        }
        for (int i = 0; i < n; ++i) {
            Soldier& s = B.soldiers[i];
            if (s.hp <= 0) continue;
            s.flash = fmaxf(0.0f, s.flash - dt * 5.0f);
            if (B.dmg[i] > 0.0f) s.flash = 1.0f;   // hit feedback
            s.hp -= B.dmg[i];
        }
        B.pFlash = fmaxf(0.0f, B.pFlash - dt * 5.0f);
        if (playerDamage > 0.0f) {
            B.pHp -= B.blocking ? playerDamage * BLOCK_MELEE_FACTOR : playerDamage;
            B.pFlash = 1.0f;
        }

        // Keep living soldiers sitting on the terrain surface (they moved in
        // x/z) — except wall posts, whose feet stay on the rampart.
        for (Soldier& s : B.soldiers)
            if (s.hp > 0 && !s.onWall) s.pos.y = B.terrain.HeightAt(s.pos.x, s.pos.z);

        // ---------- arrows in flight ----------
        for (Arrow& a : B.arrows) {
            if (!a.alive) continue;
            a.life -= dt;
            a.pos = Vector3Add(a.pos, Vector3Scale(a.vel, dt));
            a.vel.y -= ARROW_GRAVITY * dt;
            if (a.life <= 0 || a.pos.y <= B.terrain.HeightAt(a.pos.x, a.pos.z)) {
                a.alive = false;   // stuck in the dirt
                continue;
            }
            // Siege wall stops low shafts (arrows can still arc over the top).
            if (B.hasWall && fabsf(a.pos.x) > GATE_HALF &&
                fabsf(a.pos.z - WALL_Z) < WALL_BAND &&
                a.pos.y < B.terrain.HeightAt(a.pos.x, WALL_Z) + WALL_HEIGHT) {
                a.alive = false;
                continue;
            }
            // Hit a soldier of the opposing team?
            for (Soldier& s : B.soldiers) {
                if (s.hp <= 0 || s.team == a.team) continue;
                const Vector3 chest = Vector3Add(s.pos, { 0, 1.2f, 0 });
                if (Vector3DistanceSqr(a.pos, chest) < ARROW_HIT_RADIUS * ARROW_HIT_RADIUS) {
                    s.hp -= ApplyArmor(a.damage, LoadoutArmor(c, TroopLoadout(c, s.troop)));
                    s.flash = 1.0f;
                    a.alive = false;
                    break;
                }
            }
            // Hit the player?
            if (a.alive && a.team == Team::Enemy) {
                const Vector3 chest = Vector3Add(B.pPos, { 0, 1.2f, 0 });
                if (Vector3DistanceSqr(a.pos, chest) < ARROW_HIT_RADIUS * ARROW_HIT_RADIUS) {
                    float d = ApplyArmor(a.damage, LoadoutArmor(c, B.setup.heroLoadout));
                    if (B.blocking) d *= BLOCK_MISSILE_FACTOR;
                    B.pHp -= d;
                    B.pFlash = 1.0f;
                    a.alive = false;
                }
            }
        }
        B.arrows.erase(std::remove_if(B.arrows.begin(), B.arrows.end(),
                                      [](const Arrow& a) { return !a.alive; }),
                       B.arrows.end());
    }

    // Tallies for the HUD and win/lose, computed after damage is applied.
    B.aliveAllies = 0;
    B.aliveEnemies = 0;
    for (const Soldier& s : B.soldiers) {
        if (s.hp <= 0) continue;
        (s.team == Team::Enemy ? B.aliveEnemies : B.aliveAllies)++;
    }

    // ---------- win / lose ----------
    if (!B.over) {
        if (B.pHp <= 0) EndBattle(false);
        else if (B.aliveEnemies == 0) EndBattle(true);
    }
    return true;
}

void BattleDraw(const Content& c) {
    // ---------- camera ----------
    Camera3D cam = { 0 };
    const Vector3 look = { sinf(B.yaw) * cosf(B.pitch), sinf(B.pitch), cosf(B.yaw) * cosf(B.pitch) };
    const Vector3 eye = { B.pPos.x, B.pPos.y + 2.0f, B.pPos.z };
    cam.position = Vector3Subtract(eye, Vector3Scale(look, 6.0f));
    cam.position.y = fmaxf(cam.position.y, 0.5f);
    cam.target = Vector3Add(eye, Vector3Scale(look, 4.0f));
    cam.up = { 0, 1, 0 };
    cam.fovy = 60;
    cam.projection = CAMERA_PERSPECTIVE;

    // ================= DRAW =================
    BeginDrawing();
    ClearBackground(Color{ 150, 190, 230, 255 });

    BeginMode3D(cam);
    B.terrain.Draw();

    // Siege wall: stone segments with crenellations, broken by the gate.
    if (B.hasWall) {
        const Color stone = { 150, 148, 152, 255 };
        for (float x = -ARENA; x < ARENA; x += 4.0f) {
            const float cx = x + 2.0f;
            if (fabsf(cx) <= GATE_HALF + 1.0f) continue;   // the gateway
            const float gy = B.terrain.HeightAt(cx, WALL_Z);
            DrawCube({ cx, gy + WALL_HEIGHT * 0.5f, WALL_Z }, 4.0f, WALL_HEIGHT, WALL_BAND * 2, stone);
            DrawCube({ cx - 1.0f, gy + WALL_HEIGHT + 0.35f, WALL_Z }, 1.2f, 0.7f, WALL_BAND * 2,
                     Color{ 130, 128, 132, 255 });   // crenellation
        }
        // gate posts
        for (const float gx : { -GATE_HALF, GATE_HALF }) {
            const float gy = B.terrain.HeightAt(gx, WALL_Z);
            DrawCube({ gx, gy + (WALL_HEIGHT + 1.5f) * 0.5f, WALL_Z }, 1.6f,
                     WALL_HEIGHT + 1.5f, WALL_BAND * 2 + 0.4f, Color{ 110, 108, 112, 255 });
        }
    }

    // Beyond this distance a soldier draws as a cheap two-box silhouette —
    // full segmented humanoids only where the player can appreciate them.
    constexpr float LOD_DIST    = 45.0f;
    constexpr float LOD_DIST_SQ = LOD_DIST * LOD_DIST;

    for (const Soldier& s : B.soldiers) {
        if (s.hp <= 0) {
            const float gy = B.terrain.HeightAt(s.pos.x, s.pos.z);
            DrawCube({ s.pos.x, gy + 0.15f, s.pos.z }, 1.4f, 0.3f, 0.6f, Fade(DARKGRAY, 0.7f));
            continue;
        }
        if (Vector3DistanceSqr(cam.position, s.pos) > LOD_DIST_SQ) {
            const Color tint = TeamTint(s.team);
            DrawCube({ s.pos.x, s.pos.y + 0.95f, s.pos.z }, 0.7f, 1.5f, 0.45f, tint);
            DrawCube({ s.pos.x, s.pos.y + 1.85f, s.pos.z }, 0.32f, 0.32f, 0.32f,
                     Color{ 224, 188, 150, 255 });
            const float fracFar = s.hp / s.maxHp;
            DrawCube({ s.pos.x, s.pos.y + 2.5f, s.pos.z }, 1.2f * fracFar, 0.08f, 0.08f,
                     s.team == Team::Enemy ? RED : GREEN);
            continue;
        }
        Pose pose;
        pose.yaw = s.yaw;
        pose.swing = s.swing;
        pose.flash = s.flash;
        pose.walkPhase = s.walkPhase;
        pose.weapon = s.activeWeapon;   // draw whichever weapon it's wielding
        pose.accent = c.troops[s.troop].accent;   // rank plume

        Vector3 riderPos = s.pos;
        if (c.troops[s.troop].mounted) {
            // The horse: barrel, neck + head, four legs — rider sits on top.
            const float hy = s.pos.y;
            const float cy = cosf(s.yaw), sy = sinf(s.yaw);
            auto hAt = [&](float r, float u, float f) {
                return Vector3{ s.pos.x + r * cy + f * sy, hy + u, s.pos.z - r * sy + f * cy };
            };
            const Color coat = Color{ 96, 66, 40, 255 };
            const float trot = sinf(s.walkPhase) * 0.25f;
            DrawCapsule(hAt(0, 1.05f, -0.7f), hAt(0, 1.05f, 0.7f), 0.34f, 8, 5, coat);   // barrel
            DrawCapsule(hAt(0, 1.15f, 0.7f), hAt(0, 1.65f, 1.15f), 0.14f, 6, 4, coat);   // neck
            DrawCapsule(hAt(0, 1.65f, 1.15f), hAt(0, 1.55f, 1.5f), 0.11f, 6, 4, coat);   // head
            DrawCapsule(hAt(-0.2f, 0.05f,  0.55f + trot), hAt(-0.2f, 0.95f, 0.55f), 0.07f, 5, 3, coat);
            DrawCapsule(hAt( 0.2f, 0.05f,  0.55f - trot), hAt( 0.2f, 0.95f, 0.55f), 0.07f, 5, 3, coat);
            DrawCapsule(hAt(-0.2f, 0.05f, -0.55f - trot), hAt(-0.2f, 0.95f, -0.55f), 0.07f, 5, 3, coat);
            DrawCapsule(hAt( 0.2f, 0.05f, -0.55f + trot), hAt( 0.2f, 0.95f, -0.55f), 0.07f, 5, 3, coat);
            riderPos.y += 1.25f;
            pose.walkPhase = 0;   // the rider sits; the horse does the running
        }
        DrawCharacter(c, riderPos, TroopLoadout(c, s.troop), pose, TeamTint(s.team));
        // health bar (above the soldier, following the terrain)
        const float frac = s.hp / s.maxHp;
        DrawCube({ s.pos.x, s.pos.y + 2.5f, s.pos.z }, 1.2f * frac, 0.08f, 0.08f,
                 s.team == Team::Enemy ? RED : GREEN);
    }

    // arrows in flight — short shafts oriented along their velocity
    for (const Arrow& a : B.arrows) {
        const Vector3 tail = Vector3Subtract(a.pos, Vector3Scale(Vector3Normalize(a.vel), 0.6f));
        DrawCylinderEx(tail, a.pos, 0.03f, 0.03f, 4, DARKBROWN);
    }

    // player avatar
    Pose ppose;
    ppose.yaw = B.yaw;
    ppose.swing = B.swing;
    ppose.windup = B.readying ? B.windup : 0.0f;   // cocked while holding LMB
    ppose.flash = B.pFlash;
    ppose.attackDir = B.attackDir;
    ppose.blocking = B.blocking;
    ppose.walkPhase = B.walkPhase;
    ppose.weapon = B.setup.heroLoadout.get(EquipSlot::Weapon);
    DrawCharacter(c, B.pPos, B.setup.heroLoadout, ppose, Color{ 40, 120, 255, 255 });

    EndMode3D();

    // ---------- HUD ----------
    DrawRectangle(18, GetScreenHeight() - 42, 300, 22, Fade(BLACK, 0.5f));
    DrawRectangle(20, GetScreenHeight() - 40, (int)(296 * fmaxf(B.pHp, 0) / B.pMaxHp), 18, RED);
    ui::Text("HP", 24, GetScreenHeight() - 40, 18, RAYWHITE);
    ui::Text(TextFormat("Allies: %d   Enemies: %d", B.aliveAllies, B.aliveEnemies), 18, 12, 22, RAYWHITE);
    ui::Text("Hold LMB to ready a swing, release to strike | RMB block | Q swap weapon | ~ strategy",
             18, 38, 16, Fade(RAYWHITE, 0.7f));

    const char* dirName[] = { "UP", "DOWN", "LEFT", "RIGHT" };
    const int hwh = B.setup.heroLoadout.get(EquipSlot::Weapon);
    const char* wname = c.weapons.valid(hwh) ? c.weapons[hwh].name.c_str() : "Unarmed";
    ui::Text(TextFormat("Weapon: %s    Orders: %s (ranks %d)", wname,
                        FormationName(B.formation), B.ranks), 18, 60, 16, GOLD);
    if (B.readying)
        ui::Text(TextFormat("Readying swing: %s  (release!)", dirName[(int)B.attackDir]),
                 18, 82, 16, ORANGE);

    DrawLine(GetScreenWidth() / 2 - 8, GetScreenHeight() / 2, GetScreenWidth() / 2 + 8, GetScreenHeight() / 2, RAYWHITE);
    DrawLine(GetScreenWidth() / 2, GetScreenHeight() / 2 - 8, GetScreenWidth() / 2, GetScreenHeight() / 2 + 8, RAYWHITE);

    // ---- strategy / formation menu: a slightly grey, transparent side panel;
    //      gameplay stays visible and running behind it ----
    if (B.showMenu) {
        const int pw = 300;
        const int px = GetScreenWidth() - pw;
        const int ph = GetScreenHeight();
        DrawRectangle(px, 0, pw, ph, Fade(Color{ 40, 44, 52, 255 }, 0.62f));
        DrawRectangle(px, 0, 3, ph, Fade(RAYWHITE, 0.25f));
        int y = 26;
        ui::Title("STRATEGY", px + 22, y, 30, RAYWHITE);                      y += 48;
        ui::Text("Formations", px + 22, y, 20, Fade(RAYWHITE, 0.75f));        y += 30;
        const FormationType opts[] = { FormationType::Charge, FormationType::Line,
                                       FormationType::Square, FormationType::Spread };
        for (int i = 0; i < 4; ++i) {
            const bool sel = (B.formation == opts[i]);
            ui::Text(TextFormat("[%d] %s%s", i + 1, FormationName(opts[i]), sel ? "   <" : ""),
                     px + 22, y, 22, sel ? GOLD : RAYWHITE);
            y += 30;
        }
        y += 14;
        ui::Text(TextFormat("Ranks: %d", B.ranks), px + 22, y, 22, RAYWHITE);      y += 28;
        ui::Text("[ and ] : fewer / more ranks", px + 22, y, 16, Fade(RAYWHITE, 0.7f)); y += 34;
        ui::Text("Charge attacks; Line / Square /", px + 22, y, 16, Fade(RAYWHITE, 0.7f)); y += 20;
        ui::Text("Spread hold that shape and fight.", px + 22, y, 16, Fade(RAYWHITE, 0.7f)); y += 30;
        ui::Text("~ closes this menu.", px + 22, y, 16, Fade(RAYWHITE, 0.7f));
    }

    if (B.over) {
        const char* msg = B.won ? "VICTORY!" : "YOU HAVE FALLEN";
        const int w = ui::MeasureTitle(msg, 60);
        ui::Title(msg, (GetScreenWidth() - w) / 2, GetScreenHeight() / 2 - 60, 60, B.won ? GOLD : RED);
    }

    EndDrawing();
}

BattleView GetBattleView() {
    BattleView v;
    v.active       = !B.soldiers.empty() || B.pMaxHp > 0;
    v.heroPos      = B.pPos;
    v.heroYaw      = B.yaw;
    v.heroPitch    = B.pitch;
    v.heroHp       = B.pHp;
    v.heroMaxHp    = B.pMaxHp;
    v.heroWeapon   = B.setup.heroLoadout.get(EquipSlot::Weapon);
    v.aliveAllies  = B.aliveAllies;
    v.aliveEnemies = B.aliveEnemies;
    v.arrowsInFlight = (int)B.arrows.size();
    for (const Soldier& s : B.soldiers)
        if (s.onWall && s.hp > 0) v.wallDefenders++;
    v.over         = B.over;
    v.won          = B.won;
    return v;
}
