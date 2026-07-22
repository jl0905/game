#include "battle.h"
#include "character.h"
#include "../gfx.h"
#include "../sfx.h"
#include "../ui.h"
#include "../parallel.h"
#include "../settings.h"
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

// Siege climbing points (N1): every siege has two ladders flanking the gate;
// engineering adds more ladders and, with a tower, one wide rolling lane.
// Runtime state (filled in BattleInit from BattleSetup::siegePrep).
int g_dbgFrame = 0;   // DMG_DEBUG timestamping only

constexpr float LADDER_HALF = 1.5f;    // half-width of a ladder lane
constexpr float TOWER_HALF  = 3.5f;    // half-width of the tower's ramp
struct ClimbPoint { float x; float half; bool tower; };
std::vector<ClimbPoint> g_climbs;

bool NearLadder(float x) {
    for (const ClimbPoint& p : g_climbs)
        if (fabsf(x - p.x) <= p.half) return true;
    return false;
}

// Extra height a climber gains while inside the crossing band near a ladder —
// an arc that peaks at the rampart top right over the wall line.
float LadderClimbBump(float x, float z) {
    if (!NearLadder(x)) return 0.0f;
    const float span = WALL_BAND + 1.6f;              // climb starts before the band
    const float dz = fabsf(z - WALL_Z);
    if (dz >= span) return 0.0f;
    return WALL_HEIGHT * (1.0f - dz / span);
}

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
    bool         raining       = false; // some fields are fought in the rain
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

// Flat-shaded colour for a triangle from its three corners, with a little
// per-triangle jitter so the ground reads as textured, not vector-flat.
Color TriangleColor(Vector3 a, Vector3 b, Vector3 c) {
    const Vector3 n = Vector3Normalize(
        Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
    const float slope = 1.0f - fabsf(n.y);
    const float h = (a.y + b.y + c.y) / 3.0f;
    unsigned int hash = (unsigned)(a.x * 37.0f) * 73856093u ^
                        (unsigned)(a.z * 41.0f) * 19349663u;
    hash ^= hash >> 13;
    const float jitter = 0.92f + 0.16f * ((hash & 0xFF) / 255.0f);
    const float shade = (0.75f + 0.25f * fabsf(n.y)) * jitter;
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
    cfg.raining       = sinf(campaignPos.x * 0.0017f - campaignPos.y * 0.0023f) > 0.45f;
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
    auto emit = [&](Vector3 p, Color col, Vector3 nrm) {
        mesh.vertices[v * 3 + 0] = p.x;
        mesh.vertices[v * 3 + 1] = p.y;
        mesh.vertices[v * 3 + 2] = p.z;
        mesh.normals[v * 3 + 0] = nrm.x;
        mesh.normals[v * 3 + 1] = nrm.y;
        mesh.normals[v * 3 + 2] = nrm.z;
        mesh.colors[v * 4 + 0] = col.r;
        mesh.colors[v * 4 + 1] = col.g;
        mesh.colors[v * 4 + 2] = col.b;
        mesh.colors[v * 4 + 3] = col.a;
        ++v;
    };
    auto faceNormal = [](Vector3 a, Vector3 b, Vector3 cc) {
        return Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a),
                                                    Vector3Subtract(cc, a)));
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
            const Vector3 n1 = faceNormal(A, C, Bv);
            const Vector3 n2 = faceNormal(Bv, C, D);
            emit(A, c1, n1); emit(C, c1, n1); emit(Bv, c1, n1);
            emit(Bv, c2, n2); emit(C, c2, n2); emit(D, c2, n2);
        }
    }
    UploadMesh(&mesh, false);
    model_ = LoadModelFromMesh(mesh);
    model_.materials[0].shader = GetLitShader();   // sun-lit slopes
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
        DrawCylinder({ t.pos.x, t.pos.y + 0.04f, t.pos.z }, t.radius * 1.1f,
                     t.radius * 1.1f, 0.02f, 10, Fade(BLACK, 0.25f));   // shadow
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
enum class FormationType { Charge, Line, Square, Spread, ShieldWall };

// Battlefield orders (M2): free engagement, screen the hero, or hold ground.
enum class OrderType { Charge, Follow, Hold };

const char* OrderName(OrderType o) {
    switch (o) {
        case OrderType::Follow: return "Follow me";
        case OrderType::Hold:   return "Hold position";
        default:                return "Charge";
    }
}

const char* FormationName(FormationType f) {
    switch (f) {
        case FormationType::Charge: return "Charge";
        case FormationType::Line:   return "Line";
        case FormationType::Square: return "Square";
        case FormationType::Spread: return "Spread";
        case FormationType::ShieldWall: return "Shield Wall";
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
    const float sp   = type == FormationType::Spread     ? FORM_SPACING * 2.2f
                     : type == FormationType::ShieldWall ? FORM_SPACING * 0.7f   // V48
                                                         : FORM_SPACING;
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
    int     swingDir = -1;   // 0..3 melee swing direction (G4), -1 none
};

// A missile in flight. Team says who it can hurt (no friendly fire — allied
// archers shooting over a melee would otherwise slaughter their own side).
// A masterless horse (T6): its rider fell, and it survives him — bolting,
// then wandering the field, shying from the press. An entity of its own.
struct LooseHorse {
    Vector3 pos{};
    Vector3 target{};
    float   yaw = 0;
    float   hp = 0;
    float   wanderT = 0;
    float   walkPhase = 0;
    bool    yours = false;   // your own dismounted horse (U11): it waits
};

struct Arrow {
    Vector3 pos;
    Vector3 vel;
    Team    team;
    float   damage = 0;
    float   life = ARROW_LIFETIME;
    bool    alive = true;
};

// Morale & rout (G3, per-soldier since K4): every soldier carries `nerve`.
// Watching a friend die nearby drains it, watching a foe fall restores it,
// the hero falling shakes the whole line, and the hero's rally stiffens it.
// A soldier whose nerve breaks routs alone — lines crumble from where the
// dying happens. Wall garrisons hold to the death; arena bouts never rout.
// TODO(balance): every number below.
constexpr float NERVE_MAX          = 100.0f;
constexpr float NERVE_ALLY_DEATH   = 15.0f;  // a friend cut down within earshot
constexpr float NERVE_ENEMY_DEATH  = 10.0f;  // a foe cut down within earshot
constexpr float NERVE_WITNESS_R    = 8.0f;   // how far the dying carries
constexpr float NERVE_HERO_DOWN    = 20.0f;  // the banner falls
constexpr float NERVE_RALLY        = 15.0f;  // the hero's kill-cry
constexpr float NERVE_REGEN        = 2.0f;   // per second, courage returns

// Shields matter (direction G4): a shield-bearer meets a swing from his
// guarded side with wood — and the wood wears out. The guard direction is a
// deterministic per-soldier habit for now: structure, not a fencing brain.
// TODO(balance): all three numbers.
constexpr float SHIELD_HP           = 40.0f;
constexpr float SHIELD_BLOCK_FACTOR = 0.35f;  // melee damage that gets through
constexpr float SHIELD_WEAR_PER_HIT = 8.0f;

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
    float   shieldHp = SHIELD_HP;   // wood left on the arm (G4)
    float   stun = 0;          // hit-stun (T): reeling, can't move or swing
    float   stunImmune = 0;    // post-stun grace — an opening, not a lock
    bool    looted = false;    // his weapon already taken up (V39)
    int     guardDir = -1;     // reactive shield guard (T): covers where last
                               // struck; -1 = the old positional habit
    float   nerve = NERVE_MAX; // courage left (K4); at 0 the soldier breaks
    bool    routed  = false;   // broke and running for the field edge (G3)
    float   routTime = 0;      // seconds spent fleeing
    bool    escaped = false;   // off the field alive — not a casualty
    int     activeWeapon = -1; // currently-wielded weapon handle
    bool    onWall = false;    // garrison archer posted on the siege wall
    float   trampleCd = 0;     // mounted: cooldown between trample hits
    float   horseHp = 0;       // mount's health (mounted troops only)
    bool    dehorsed = false;  // horse killed under them; fighting on foot
};

// Uniform spatial grid over the XZ plane (direction G1), rebuilt once per tick
// before the parallel AI phase. Every proximity question in the hot path —
// target search, separation, line-break, trample — asks the grid instead of
// scanning all soldiers, turning the per-tick cost from O(n^2) into ~O(n).
struct SoldierGrid {
    static constexpr float CELL = 6.0f;   // a bit over two soldier diameters
    float minX = 0, minZ = 0;
    int   w = 1, h = 1;
    std::vector<int> heads;   // cell -> first live soldier index, or -1
    std::vector<int> next;    // soldier -> next in the same cell, or -1

    int CellX(float x) const { return (int)((x - minX) / CELL); }
    int CellZ(float z) const { return (int)((z - minZ) / CELL); }

    void Build(const std::vector<Soldier>& sol) {
        const int n = (int)sol.size();
        next.assign(n, -1);
        if (n == 0) { heads.assign(1, -1); w = h = 1; minX = minZ = 0; return; }
        float maxX = -1e9f, maxZ = -1e9f;
        minX = 1e9f; minZ = 1e9f;
        for (const Soldier& s : sol) {
            if (s.hp <= 0 || s.escaped) continue;
            minX = fminf(minX, s.pos.x); maxX = fmaxf(maxX, s.pos.x);
            minZ = fminf(minZ, s.pos.z); maxZ = fmaxf(maxZ, s.pos.z);
        }
        if (minX > maxX) { minX = minZ = 0; maxX = maxZ = 0; }   // everyone dead
        w = (int)((maxX - minX) / CELL) + 1;
        h = (int)((maxZ - minZ) / CELL) + 1;
        heads.assign((size_t)w * h, -1);
        for (int i = 0; i < n; ++i) {
            if (sol[i].hp <= 0 || sol[i].escaped) continue;
            const int cell = CellZ(sol[i].pos.z) * w + CellX(sol[i].pos.x);
            next[i] = heads[cell];
            heads[cell] = i;
        }
    }

    // Visit every live soldier index whose cell overlaps the radius around p.
    template <typename F>
    void ForNeighbors(Vector3 p, float radius, F&& f) const {
        const int x0 = std::max(0, CellX(p.x - radius));
        const int x1 = std::min(w - 1, CellX(p.x + radius));
        const int z0 = std::max(0, CellZ(p.z - radius));
        const int z1 = std::min(h - 1, CellZ(p.z + radius));
        for (int cz = z0; cz <= z1; ++cz)
            for (int cx = x0; cx <= x1; ++cx)
                for (int i = heads[(size_t)cz * w + cx]; i >= 0; i = next[i]) f(i);
    }
};

// Battle pacing (direction J2): soldiers recover their swings slower and close
// a shade less fast than the hero, so fights read as lines grinding rather
// than instant blenders — and the player's full-speed hands are a real edge.
// TODO(balance): both scales.
constexpr float PACE_COOLDOWN_SCALE = 1.5f;
constexpr float PACE_MOVE_SCALE     = 0.85f;

// Hero presence (direction J3): allies fighting inside the champion's aura
// recover their swings faster, and a kill by the hero's own hand rings a
// rally pulse that briefly doubles the aura. TODO(balance): all three.
constexpr float RALLY_RADIUS         = 12.0f;
constexpr float RALLY_COOLDOWN_SCALE = 0.75f;
constexpr float RALLY_PULSE_TIME     = 4.0f;

constexpr float ROUT_ESCAPE_TIME = 10.0f;   // seconds of flight to get away
constexpr float ROUT_SPEED_SCALE = 1.15f;   // fear lends speed

// Cavalry trample. TODO(balance): damage/cooldown; structure only.
constexpr float TRAMPLE_DAMAGE   = 15.0f;
constexpr float TRAMPLE_COOLDOWN = 1.5f;
constexpr float TRAMPLE_RADIUS   = 1.3f;

// Horses are mortal. TODO(balance): mount HP and the share of blows that
// strike the mount instead of the rider.
constexpr float HORSE_HP        = 60.0f;
constexpr float HORSE_HIT_SHARE = 0.15f;   // playtest-tuned (T6): at 0.4 the
                                           // horse died with its rider; now
                                           // killing the man leaves a mount

struct BattleState {
    BattleSetup          setup;     // world-map snapshot this battle runs on
    Terrain              terrain;   // generated from setup.campaignPos
    bool                 hasWall = false;   // siege of a walled settlement
    bool                 raining = false;
    bool                 night   = false;   // archery suffers in the dark (V44)
    std::vector<Soldier> soldiers;
    std::vector<Arrow>   arrows;    // missiles in flight
    std::vector<LooseHorse> looseHorses;   // mounts that outlived their riders (T6)
    std::vector<Vector4> props;     // ground dressing (V7): xyz + w kind
                                    // (0 grass tuft, 1 rock); deterministic
    std::vector<Vector3> stains;    // where men fell (V12): the field
                                    // remembers until the battle ends
    Vector3 prevHeroPos{};          // for momentum (T5): last frame's position
    float   heroSpeed = 0;          // hero's current ground speed

    // Feedback particles (blood, hoof dust); purely visual.
    struct Puff { Vector3 pos, vel; float life; float maxLife; Color col; };
    std::vector<Puff> puffs;

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
    bool    mounted = false;    // the hero rides in field battles (not sieges)
    float   pTrampleCd = 0;
    float   pHorseHp = 0;       // the hero's mount can be killed under him
    float   shake = 0;          // camera kick when the hero is struck

    // Hero battlefield impact (J3).
    float rallyPulse = 0;   // seconds of widened aura left after a hero kill
    int   heroKills  = 0;   // shown in the HUD; the player's mark on the field

    // Morale & rout (G3).
    int   startPlayerSide = 0, startEnemySide = 0;   // spawned strength per side
    bool  playerSideRouted = false, enemySideRouted = false;
    float routBanner = 0;                            // "THEY BREAK" fade
    // Battle standards (V32): one bannerman a side. His tall standard marks
    // the line from across the field; his death shakes his side's nerve and
    // the banner passes to the nearest living hand.
    int   bannerIdx[2] = { -1, -1 };   // [0]=player side, [1]=enemy side
    float kickCd = 0;                  // the boot's recovery (V33)
    int   heroKicksLanded = 0;
    std::string pickupMsg;             // "TAKEN UP: ..." caption (V39)
    float       pickupTimer = 0;
    std::vector<int> surrendered;      // enemies who yielded, per troop (V42)
    float bannerFlash  = 0;            // "THE BANNER FALLS" fade
    bool  bannerFellOurs = false;      // whose banner just fell
    const char* routText = "";

    bool  over = false;
    bool  won = false;
    bool  heroDown = false;   // struck senseless; the warband fights on
    float overTimer = 0;
    float introTimer = 0;   // opening banner fade
    bool  reported = false;         // outcome handed back to the caller
    int   aliveAllies = 0, aliveEnemies = 0;   // tallied each update, drawn in HUD

    // Manual mouse tracking (GetMouseDelta unreliable under WSL/X11).
    Vector2 lastMouse{ 0, 0 };
    bool    hasLastMouse = false;
    Vector2 aimAccum{ 0, 0 };       // recent mouse motion, for attack direction
    float   soakFlash = 0;          // your swing just met wood (U5): teach it
    float   parryFlash = 0;         // you just blocked a hit (U5): say so

    // Combat: hold LMB to ready a swing, release to strike.
    bool  readying = false;
    float windup = 0.0f;

    // Hero arsenal (a hero may carry several weapons and switch between them).
    std::vector<int> heroArsenal;
    int  heroWeapon = 0;

    // Formations / strategy menu.
    FormationType formation = FormationType::Charge;

    // Deployment (R2): a device-side pause before the lines close. Armed at
    // init; the first *windowed* gather trips it (headless runs never gather,
    // so scripted sims are bit-identical). SPACE sounds the horn.
    bool deployArmed = false;
    bool deploying   = false;

    // Battlefield orders (M2): what the player's own troops are doing with
    // the formation shape — charging freely, following the hero's banner, or
    // holding ground where the order was barked (F1/F2/F3 mid-fight).
    OrderType order = OrderType::Charge;
    Vector3   holdPos{};            // anchor frozen when Hold was ordered
    int   ranks = 3;
    bool  showMenu = false;
    int   ownCount = 0;             // player's own (non-ally) troop count
    int   enemyCount = 0;           // enemy line strength at the start
    bool  enemyHoldsLine = false;   // field armies form up before they charge
    bool  enemyCharged = false;     // the moment the whole line breaks forward
    float cryTimer = 0;             // war-cry banner fade
    const char* cryText = "THEY CHARGE!";   // what the banner shouts

    // Parallel-AI scratch, reused each frame to avoid per-frame allocation.
    std::vector<AICmd> cmds;
    std::vector<float> dmg;
    SoldierGrid        grid;       // proximity index, rebuilt each tick (G1)
    std::vector<int>   targeted;   // how many foes aim at each soldier (J1)

    // Per-troop fighting gear for this battle (K6): defaults + overrides.
    std::vector<Loadout> troopGear;
};

BattleState B;

const Loadout& TroopLoadout(const Content& c, int troop) {
    if (troop >= 0 && troop < (int)B.troopGear.size()) return B.troopGear[troop];
    return c.troops[troop].loadout;
}

// Pick an attack direction from accumulated mouse motion.
AttackDir DirFromMotion(Vector2 m) {
    if (fabsf(m.x) > fabsf(m.y))
        return m.x > 0 ? AttackDir::Right : AttackDir::Left;
    return m.y < 0 ? AttackDir::Up : AttackDir::Down;
}

// The gear a troop actually fights in: the per-battle table (content defaults
// with the setup's K6 overrides applied), falling back to the catalogue.
const Loadout& TroopLoadout(const Content& c, int troop);   // defined after BattleState

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
    // A quarter always gets through (V22): plate blunts peasant swords
    // without making knights literally unkillable — the flat-1 floor
    // stalled battles once armour awoke (V15). TODO(balance): the curve.
    return fmaxf(damage - (float)armor, damage * 0.25f);
}

// Riding someone down with a polearm levelled is a couched lance strike —
// weapon damage carried by the horse's momentum. Anything else just tramples.
// TODO(balance): the couch multiplier.
float TrampleDamage(const Content& c, int weapon) {
    if (c.weapons.valid(weapon) && c.weapons[weapon].wclass == WeaponClass::Polearm)
        return WeaponDamage(c, weapon) * 2.0f;
    return TRAMPLE_DAMAGE;
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
            s.slot = ally ? -1 : n;   // formation slot (players and enemy lines)
            s.activeWeapon = c.troops[troop].loadout.weaponAt(0);
            s.horseHp = HORSE_HP;
            const float x = -20.0f + (n % 10) * 4.0f;
            const float z = zBase + (n / 10) * 4.0f * (team == Team::Enemy ? 1.0f : -1.0f);
            s.pos = { x, B.terrain.HeightAt(x, z), z };
            s.yaw = (team == Team::Enemy) ? PI : 0.0f;
            B.soldiers.push_back(s);
            ++n;
        }
    }
}

// A soldier still fighting from the saddle.
bool IsMounted(const Content& c, const Soldier& s) {
    return c.troops[s.troop].mounted && !s.dehorsed;
}

// Particle helpers; pure feedback, no gameplay.
unsigned int PuffRand() {
    static unsigned int h = 12345u;
    h ^= h << 13; h ^= h >> 17; h ^= h << 5;
    return h;
}

void SpawnBlood(Vector3 at) {
    if (!GetSettings().particles) return;
    for (int i = 0; i < 5; ++i) {
        const unsigned int h = PuffRand();
        const float a = (h & 0xFF) / 255.0f * 2.0f * PI;
        const float up = 2.0f + ((h >> 8) & 0x7F) / 127.0f * 2.0f;
        B.puffs.push_back({ Vector3Add(at, { 0, 1.2f, 0 }),
                            { cosf(a) * 1.6f, up, sinf(a) * 1.6f },
                            0.45f, 0.45f, Color{ 168, 24, 24, 255 } });
    }
}

// Bright sparks off a raised guard — a parry you can SEE.
void SpawnSparks(Vector3 at) {
    for (int i = 0; i < 6; ++i) {
        const unsigned int h = PuffRand();
        const float a = (h & 0xFF) / 255.0f * 2.0f * PI;
        const float up = 1.0f + ((h >> 8) & 0x7F) / 127.0f * 2.5f;
        B.puffs.push_back({ Vector3Add(at, { 0, 1.3f, 0 }),
                            { cosf(a) * 2.4f, up, sinf(a) * 2.4f },
                            0.22f, 0.22f, Color{ 255, 232, 140, 255 } });
    }
}

// Kicked-up earth behind a galloping horse.
void SpawnDust(Vector3 at) {
    if (!GetSettings().particles) return;
    const unsigned int h = PuffRand();
    const float a = (h & 0xFF) / 255.0f * 2.0f * PI;
    B.puffs.push_back({ Vector3Add(at, { 0, 0.25f, 0 }),
                        { cosf(a) * 0.7f, 1.1f, sinf(a) * 0.7f },
                        0.6f, 0.6f, Color{ 148, 128, 100, 255 } });
}

// All damage to a soldier routes through here so a mounted target's horse
// soaks its share of the blow — and can die under the rider.
// Hit-stun (T): a blow that lands (not soaked to a scratch) staggers the
// victim — no moving, no swinging while reeling. This is what makes blocking
// and swing-direction craft matter. TODO(balance): duration/threshold.
constexpr float STUN_TIME     = 0.45f;
constexpr float STUN_MIN_DMG  = 4.0f;   // shield-soaked taps don't stagger

// The field remembers (V12): mark where a man fell. Capped so a rout
// doesn't paint the whole plain.
void StainGround(const Vector3& p) {
    if (B.stains.size() < 240) B.stains.push_back(p);
}

// A slain rider's mount survives him (T6): it bolts free and wanders.
void FreeHorse(const Content& c, const Soldier& s) {
    if (!c.troops.valid(s.troop) || !c.troops[s.troop].mounted) return;
    if (s.dehorsed || s.horseHp <= 0) return;
    LooseHorse h;
    h.pos = s.pos;
    h.yaw = s.yaw;
    h.hp  = s.horseHp;
    h.target  = s.pos;
    h.wanderT = 0;
    B.looseHorses.push_back(h);
}

void DamageSoldier(const Content& c, Soldier& s, float dmg) {
#ifdef DMG_DEBUG
    if (s.hp - dmg <= 0 || (IsMounted(c, s) && s.horseHp - dmg * 0.4f <= 0))
        printf("DEATHISH f=%d team=%d troop=%d dmg=%.1f hp=%.0f horse=%.0f\n",
               g_dbgFrame, (int)s.team, s.troop, dmg, s.hp, s.horseHp);
#endif
    if (IsMounted(c, s)) {
        const float toHorse = dmg * HORSE_HIT_SHARE;
        s.horseHp -= toHorse;
        dmg -= toHorse;
        if (s.horseHp <= 0) s.dehorsed = true;   // horse falls; rider fights on
    }
    // A braced wall turns blades (V48): the player's own foot in Shield Wall
    // soak a third of everything — arrows and steel alike. TODO(balance).
    if (B.formation == FormationType::ShieldWall &&
        s.team == Team::Player && !s.ally && !IsMounted(c, s))
        dmg *= 0.65f;
    s.hp -= dmg;
    s.flash = 1.0f;
    if (s.hp <= 0) StainGround(s.pos);   // the field remembers (V12)
    if (dmg >= STUN_MIN_DMG && s.stunImmune <= 0) {
        s.stun       = STUN_TIME;          // reeling (T)
        s.stunImmune = STUN_TIME + 0.9f;   // then finds his feet — a crowd
        s.swing      = 0;                  // can't chain-lock a man forever
    }
    SpawnBlood(s.pos);
    // Volume falls off with distance from the hero (rough but effective).
    const float d = Vector3Distance(s.pos, B.pPos);
    SfxPlay(Sfx::Thud, Clamp(1.0f - d / 45.0f, 0.05f, 1.0f));
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
            s.horseHp = HORSE_HP;
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

// Targeting (direction J1): pick a foe by score, not blind distance. Soldiers
// spread across the enemy instead of dogpiling, turn on whoever is attacking
// them, and archers favour unshielded marks. Weights are flat TODO(balance).
constexpr float TARGET_CROWD_PENALTY   = 2.0f;  // per foe already aiming at him
constexpr float TARGET_ATTACKER_BONUS  = 4.0f;  // he is attacking me
constexpr float TARGET_UNSHIELDED_BONUS = 3.0f; // archers: no shield to raise

// A one-handed sidearm implies a shield on the arm (same inference the
// renderer uses); anything else leaves the target open to arrows.
bool HasShield(const Content& c, const Soldier& o) {
    return c.weapons.valid(o.activeWeapon) &&
           c.weapons[o.activeWeapon].wclass == WeaponClass::OneHanded;
}

// The side a soldier guards this moment: where he was last struck (T,
// reactive — so an attacker who varies his swings gets through), falling
// back to the old positional habit before first blood (G4).
int GuardDir(int idx, const Soldier& s) {
    if (s.guardDir >= 0) return s.guardDir;
    return (idx * 5 + (int)(s.walkPhase * 3.0f)) & 3;
}

// Route one melee blow through the target's shield (G4): a swing into the
// guarded side is mostly wood, and the wood wears. Returns the damage left.
float ShieldSoak(const Content& c, int victimIdx, Soldier& v, int swingDir,
                 float damage) {
    if (swingDir < 0 || !HasShield(c, v) || v.shieldHp <= 0) return damage;
    if (GuardDir(victimIdx, v) != swingDir) {
        v.guardDir = swingDir;   // he covers the side that just bled (T)
        return damage;
    }
    v.shieldHp -= SHIELD_WEAR_PER_HIT;
    return damage * SHIELD_BLOCK_FACTOR;
}

// Grid-accelerated scored target search: scan outward in growing radii and
// keep the best-scoring candidate. One extra sweep after the first hit lets a
// slightly-farther but better-scoring foe win without walking the whole map.
int FindTarget(const Content& c, int self, Team wantTeam, bool imRanged) {
    const Soldier& me = B.soldiers[self];
    int   best = -1;
    float bestScore = 1e9f;
    const float maxSpan = fmaxf(B.grid.w, B.grid.h) * SoldierGrid::CELL + 1.0f;
    for (float radius = SoldierGrid::CELL * 2; radius <= maxSpan * 2; radius *= 2) {
        B.grid.ForNeighbors(me.pos, radius, [&](int j) {
            const Soldier& o = B.soldiers[j];
            if (o.team != wantTeam) return;
            float score = Vector3Distance(me.pos, o.pos);
            if (j < (int)B.targeted.size())
                score += TARGET_CROWD_PENALTY * (float)B.targeted[j];
            if (o.target == self) score -= TARGET_ATTACKER_BONUS;
            if (imRanged && !HasShield(c, o)) score -= TARGET_UNSHIELDED_BONUS;
            if (score < bestScore) { bestScore = score; best = j; }
        });
        if (best >= 0) {
            // Grown past the best hit's shell — a wider sweep can't beat it
            // by more than the flat bonuses, so stop.
            if (radius >= bestScore + TARGET_ATTACKER_BONUS +
                              TARGET_UNSHIELDED_BONUS + TARGET_CROWD_PENALTY)
                break;
        }
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

    // A broken soldier runs for his own field edge and never strikes back.
    if (s.routed) {
        cmd.newTarget = -1;
        const Vector3 away = { 0, 0, s.team == Team::Player ? -1.0f : 1.0f };
        cmd.yaw = atan2f(away.x, away.z);
        const float ms = c.troops[s.troop].moveSpeed * ROUT_SPEED_SCALE *
                         (s.dehorsed ? 0.5f : 1.0f);
        cmd.step    = Vector3Scale(away, ms * dt);
        cmd.walkAdd = dt * 12.0f;
        return cmd;
    }

    const Team foe = (s.team == Team::Player) ? Team::Enemy : Team::Player;
    const Loadout& lo = TroopLoadout(c, s.troop);
    const bool amRanged = c.weapons.valid(s.activeWeapon) &&
                          c.weapons[s.activeWeapon].isRanged();
    int target = s.target;
    if (target < 0 || target >= (int)B.soldiers.size() ||
        B.soldiers[target].hp <= 0 || B.soldiers[target].team == s.team) {
        target = FindTarget(c, i, foe, amRanged);
    }
    cmd.newTarget = target;

    // Enemies may prefer the player if closer than their nearest soldier foe.
    bool    targetPlayer = false;
    bool    haveFoe = false;
    Vector3 tpos{};
    if (s.team == Team::Enemy && B.pHp > 0) {
        const float dp = Vector3DistanceSqr(s.pos, B.pPos);
        if (target < 0 || dp < Vector3DistanceSqr(s.pos, B.soldiers[target].pos)) {
            targetPlayer = true; tpos = B.pPos; haveFoe = true;
        }
    }
    if (!targetPlayer && target >= 0) { tpos = B.soldiers[target].pos; haveFoe = true; }

    float foeDist = 1e9f;
    if (haveFoe) { Vector3 t = Vector3Subtract(tpos, s.pos); t.y = 0; foeDist = Vector3Length(t); }

    // Pick the best carried weapon for this distance (multi-weapon support).
    cmd.activeWeapon = PickWeaponForRange(c, lo, haveFoe ? foeDist : FIST_REACH);
    const WeaponDef* wd = c.weapons.valid(cmd.activeWeapon) ? &c.weapons[cmd.activeWeapon] : nullptr;
    const bool ranged = wd && wd->isRanged();
    // Archers advance until comfortably inside missile range, then stand and
    // loose; melee closes to arm's reach.
    // Rain fights too (R1): wet strings throw short. And so does night
    // (V44): you cannot loose at what you cannot see. TODO(balance).
    const float rainRange  = B.raining ? 0.6f : 1.0f;
    const float nightRange = B.night ? 0.55f : 1.0f;
    const float engage = ranged ? wd->missileRange * 0.9f * rainRange * nightRange
                                : WeaponReach(c, cmd.activeWeapon) * 0.8f +
                                      (IsMounted(c, s) ? 0.9f : 0.0f);   // T5

    const bool commanded =
        (s.team == Team::Player && !s.ally && formation != FormationType::Charge);
    // Enemy field armies keep a battle line until the fight comes near.
    // The whole line breaks at once (B.enemyCharged), never soldier by soldier
    // — but a soldier with a foe already on him always defends himself.
    const bool enemyInLine =
        s.team == Team::Enemy && B.enemyHoldsLine && !B.enemyCharged &&
        s.slot >= 0 && !(haveFoe && foeDist < 10.0f);

    Vector3 goal;
    bool holding = false;
    if (enemyInLine) {
        goal = FormationTarget(FormationType::Line, 4, { 0, s.pos.y, 30.0f }, PI,
                               s.slot, B.enemyCount);
        holding = true;
    } else if (commanded && !(haveFoe && foeDist <= engage * 1.3f)) {
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
        // Deliberate pacing, quickened for allies fighting beside the hero.
        float cdScale = PACE_COOLDOWN_SCALE;
        const float aura = RALLY_RADIUS * (B.rallyPulse > 0 ? 2.0f : 1.0f);
        if (s.team == Team::Player && B.pHp > 0 &&
            Vector3DistanceSqr(s.pos, B.pPos) < aura * aura)
            cdScale *= RALLY_COOLDOWN_SCALE;
        if (ranged && B.raining) cdScale *= 1.4f;   // slow, sodden nocking (R1)
        if (ranged && B.night)   cdScale *= 1.25f;  // aiming by moonlight (V44)
        if (B.formation == FormationType::ShieldWall &&
            s.team == Team::Player && !s.ally)
            cdScale *= 1.3f;   // swinging from behind a braced shield (V48)
        cmd.newCooldown = WeaponCooldown(c, cmd.activeWeapon) * cdScale;
        cmd.newSwing    = 1.0f;
        cmd.hitDamage   = WeaponDamage(c, cmd.activeWeapon);
        if (ranged)           { cmd.shoot = true; cmd.shootAt = tpos; }
        else if (targetPlayer) cmd.hitPlayer = true;
        else {
            cmd.hitSoldier = target;
            // Each soldier swings by habit, varied by his stride (G4).
            cmd.swingDir = (i * 7 + (int)(s.walkPhase * 3.0f)) & 3;
        }
    } else if (dist > (holding ? 0.4f : engage)) {
        // A dehorsed rider trudges at half pace (his boots, not his horse).
        // A shield wall advances at a brace's pace (V48). TODO(balance).
        const bool inWall = B.formation == FormationType::ShieldWall &&
                            s.team == Team::Player && !s.ally && !IsMounted(c, s);
        const float ms = c.troops[s.troop].moveSpeed * PACE_MOVE_SCALE *
                         (s.dehorsed ? 0.5f : 1.0f) * (inWall ? 0.6f : 1.0f);
        cmd.step    = Vector3Scale(Vector3Normalize(to), ms * dt);
        cmd.walkAdd = dt * 10.0f;
    }

    // Separation: sum pushes from crowding neighbours (read-only, via grid).
    Vector3 sep{ 0, 0, 0 };
    B.grid.ForNeighbors(s.pos, SOLDIER_RADIUS * 2, [&](int j) {
        if (j == i) return;
        Vector3 d = Vector3Subtract(s.pos, B.soldiers[j].pos);
        d.y = 0;
        const float len = Vector3Length(d);
        if (len < SOLDIER_RADIUS * 2 && len > 0.001f)
            sep = Vector3Add(sep, Vector3Scale(Vector3Normalize(d),
                                               (SOLDIER_RADIUS * 2 - len) * 0.5f));
    });
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

// Soft blob shadow pinned to the terrain — the cheapest depth cue there is.
void BlobShadow(const Terrain& t, float x, float z, float r) {
    DrawCylinder({ x, t.HeightAt(x, z) + 0.04f, z }, r, r, 0.02f, 12,
                 Fade(BLACK, 0.28f));
}

// The horse: barrel, neck + head, four trotting legs. The rider is drawn by
// the caller, seated 1.25 above `pos`.
void DrawHorse(Vector3 pos, float yaw, float walkPhase) {
    const float cy = cosf(yaw), sy = sinf(yaw);
    auto hAt = [&](float r, float u, float f) {
        return Vector3{ pos.x + r * cy + f * sy, pos.y + u, pos.z - r * sy + f * cy };
    };
    const Color coat = Color{ 96, 66, 40, 255 };
    const float trot = sinf(walkPhase) * 0.25f;
    DrawCapsule(hAt(0, 1.05f, -0.7f), hAt(0, 1.05f, 0.7f), 0.34f, 8, 5, coat);   // barrel
    DrawCapsule(hAt(0, 1.15f, 0.7f), hAt(0, 1.65f, 1.15f), 0.14f, 6, 4, coat);   // neck
    DrawCapsule(hAt(0, 1.65f, 1.15f), hAt(0, 1.55f, 1.5f), 0.11f, 6, 4, coat);   // head
    DrawCapsule(hAt(-0.2f, 0.05f,  0.55f + trot), hAt(-0.2f, 0.95f, 0.55f), 0.07f, 5, 3, coat);
    DrawCapsule(hAt( 0.2f, 0.05f,  0.55f - trot), hAt( 0.2f, 0.95f, 0.55f), 0.07f, 5, 3, coat);
    DrawCapsule(hAt(-0.2f, 0.05f, -0.55f - trot), hAt(-0.2f, 0.95f, -0.55f), 0.07f, 5, 3, coat);
    DrawCapsule(hAt( 0.2f, 0.05f, -0.55f + trot), hAt( 0.2f, 0.95f, -0.55f), 0.07f, 5, 3, coat);
}

// Keep a mover out of the wall band unless it's inside the gate opening.
void EnforceWall(Vector3& p) {
    if (!B.hasWall) return;
    if (fabsf(p.x) <= GATE_HALF) return;                 // in the gateway
    if (NearLadder(p.x)) return;                         // scaling a ladder
    const float dz = p.z - WALL_Z;
    if (fabsf(dz) >= WALL_BAND) return;
    p.z = WALL_Z + (dz < 0 ? -WALL_BAND : WALL_BAND);    // push back to own side
}

// If the straight path to `goal` crosses the wall away from any opening,
// steer via the nearest crossing — the gate mouth or a siege ladder — on the
// mover's own side first.
Vector3 FunnelThroughGate(Vector3 pos, Vector3 goal) {
    if (!B.hasWall) return goal;
    const bool crossing = (pos.z - WALL_Z) * (goal.z - WALL_Z) < 0;
    if (!crossing) return goal;
    if (fabsf(pos.x) <= GATE_HALF * 0.8f || NearLadder(pos.x)) return goal;
    float cx = 0.0f;                                     // gate, or closer climb
    for (const ClimbPoint& p : g_climbs)
        if (fabsf(pos.x - p.x) < fabsf(pos.x - cx)) cx = p.x;
    const float side = pos.z < WALL_Z ? -1.0f : 1.0f;
    return { cx, pos.y, WALL_Z + side * 2.5f };
}

void EndBattle(bool won) {
    B.over = true;
    B.won = won;
    B.overTimer = 2.5f;
    SfxPlay(won ? Sfx::Fanfare : Sfx::Knell);
    if (IsWindowReady()) EnableCursor();   // headless harness has no window
}

// Fight it on paper (V41): expected-strength resolution for a battle the
// player would rather not ride. Steel and armour count, but nobody's
// swordsmanship does — the auto-resolved field is always bloodier for you
// than a fight you lead. Losses land on real soldiers so the normal
// outcome bookkeeping (per-troop losses, horses, banners) just works.
// TODO(balance): every constant.
void AutoResolve(const Content& c) {
    auto power = [&](const Soldier& s) {
        const Loadout& lo = TroopLoadout(c, s.troop);
        const int w = lo.weaponCount() > 0 ? lo.weaponAt(0) : -1;
        return s.hp * (WeaponDamage(c, w) + (float)LoadoutArmor(c, lo));
    };
    float pP = B.pHp * 30.0f;   // the hero counts for a squad, no more
    float pE = 0;
    for (const Soldier& s : B.soldiers) {
        if (s.hp <= 0 || s.escaped) continue;
        (s.team == Team::Enemy ? pE : pP) += power(s);
    }
    const bool  won   = pP >= pE;
    const float wPow  = won ? pP : pE, lPow = won ? pE : pP;
    const float wLoss = wPow > 1.0f ? Clamp(0.55f * lPow / wPow, 0.10f, 0.85f)
                                    : 0.85f;
    int k = 0;
    for (Soldier& s : B.soldiers) {
        if (s.hp <= 0 || s.escaped) continue;
        const bool winnerSide = (s.team == Team::Enemy) != won;
        const bool dies = winnerSide ? ((k++ % 100) < (int)(wLoss * 100.0f))
                                     : ((k++ % 10) < 9);   // the beaten side is ruined
        if (dies) {
            s.hp = 0;
            FreeHorse(c, s);
            StainGround(s.pos);
        }
    }
    if (!won) B.pHp = 1.0f;   // carried off the field, not killed
    B.deploying = false;
    EndBattle(won);
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
    B.deployArmed = !setup.arena;   // the ring waits for no plans (R2)
    TerrainConfig tcfg = TerrainConfigFromWorld(setup.campaignPos);
    // A modded world biome (K8) overrides the built-in noise when provided.
    if (setup.hilliness >= 0) {
        tcfg.hilliness   = Clamp(setup.hilliness, 0.0f, 1.0f);
        tcfg.mountainous = tcfg.hilliness > 0.72f;
    }
    if (setup.forestDensity >= 0)
        tcfg.forestDensity = Clamp(setup.forestDensity, 0.0f, 1.0f);
    if (setup.arena) {   // a flat sanded ring under an open sky (G2)
        tcfg.hilliness = 0;
        tcfg.forestDensity = 0;
        tcfg.hasRiver = false;
        tcfg.mountainous = false;
        tcfg.raining = false;
    }
    B.terrain.Generate(tcfg, ARENA);

    // Ground dressing (V7): tufts and stones seeded by the battlefield —
    // the same field always wears the same grass. Not in the sanded ring.
    B.props.clear();
    if (!setup.arena) {
        unsigned int ph = tcfg.seed * 2654435761u + 17u;
        for (int i = 0; i < 150; ++i) {
            ph = ph * 1664525u + 1013904223u;
            const float px = (float)(ph % 170u) - 85.0f;
            const float pz = (float)((ph >> 9) % 170u) - 85.0f;
            if (B.hasWall && fabsf(pz - WALL_Z) < WALL_BAND + 1.5f) continue;
            B.props.push_back({ px, B.terrain.HeightAt(px, pz), pz,
                                (float)((ph >> 20) & 3 ? 0 : 1) });
        }
    }
    B.raining = tcfg.raining;
    // Night falls on the archery (V44): same clock the sky uses.
    B.night = setup.timeOfDay >= 0.82f || setup.timeOfDay < 0.06f;
    B.hasWall = setup.siege && setup.siegeType != SettlementType::Village;

    // Siege engineering (N1): the standing pair of ladders, plus whatever
    // the attackers built in camp. TODO(balance): positions and counts.
    g_climbs.clear();
    if (B.hasWall) {
        g_climbs.push_back({ -14.0f, LADDER_HALF, false });
        g_climbs.push_back({  14.0f, LADDER_HALF, false });
        if (setup.siegePrep >= 1) {
            g_climbs.push_back({ -26.0f, LADDER_HALF, false });
            g_climbs.push_back({  26.0f, LADDER_HALF, false });
        }
        if (setup.siegePrep >= 2)
            g_climbs.push_back({ -20.0f, TOWER_HALF, true });
    }

    // Fighting gear (K6): catalogue defaults, then the setup's overrides.
    B.troopGear.clear();
    B.troopGear.reserve(c.troops.size());
    for (int t = 0; t < c.troops.size(); ++t)
        B.troopGear.push_back(c.troops[t].loadout);
    for (const auto& ov : setup.gearOverrides)
        if (ov.first >= 0 && ov.first < (int)B.troopGear.size())
            B.troopGear[ov.first] = ov.second;

    SpawnLine(c, Team::Player, setup.playerTroops, -30.0f);
    if (B.hasWall) SpawnGarrison(c, setup.enemyTroops);   // walls + yard posts
    else           SpawnLine(c, Team::Enemy, setup.enemyTroops, 30.0f);
    // An allied party, if one joined the fight, forms up just behind your line.
    if (!setup.allyTroops.empty())
        SpawnLine(c, Team::Player, setup.allyTroops, -48.0f, /*ally=*/true);

    // Count the player's own troops (formation slots were assigned in SpawnLine)
    // and set up the hero's carried weapons.
    B.ownCount = 0;
    B.enemyCount = 0;
    for (const Soldier& s : B.soldiers) {
        if (s.team == Team::Player && !s.ally) ++B.ownCount;
        if (s.team == Team::Enemy) ++B.enemyCount;
    }
    B.enemyHoldsLine = !setup.siege && !setup.arena;   // armies form up; bouts just start
    B.startPlayerSide = (int)B.soldiers.size() - B.enemyCount;   // own + allies
    B.startEnemySide  = B.enemyCount;

    B.heroArsenal.clear();
    for (int i = 0; i < setup.heroLoadout.weaponCount(); ++i)
        B.heroArsenal.push_back(setup.heroLoadout.weaponAt(i));
    if (B.heroArsenal.empty()) B.heroArsenal.push_back(-1);
    B.heroWeapon = 0;
    B.setup.heroLoadout.set(EquipSlot::Weapon, B.heroArsenal[0]);

    B.pPos = { 0, B.terrain.HeightAt(0.0f, -38.0f), -38 };
    B.pMaxHp = (float)setup.heroMaxHp;
    B.pHp = B.pMaxHp;
    B.mounted = !setup.siege && !setup.arena;   // walls and rings are fought on foot
    B.pHorseHp = HORSE_HP;

    // Hungry men fight shaken (V37): the player's own line starts with a
    // third of its nerve gone. Allies and enemies ate. TODO(balance).
    if (setup.hungry)
        for (Soldier& s : B.soldiers)
            if (s.team == Team::Player && !s.ally) s.nerve *= 0.65f;

    // Raise the standards (V32): the first man of each side carries the
    // banner. Bouts in the ring fight without colours.
    B.bannerIdx[0] = B.bannerIdx[1] = -1;
    if (!setup.arena)
        for (int i = 0; i < (int)B.soldiers.size(); ++i) {
            const int side = B.soldiers[i].team == Team::Enemy ? 1 : 0;
            if (B.bannerIdx[side] < 0 && B.soldiers[i].hp > 0) B.bannerIdx[side] = i;
        }
    B.bannerFlash = 0;

    B.introTimer = 3.0f;
    if (IsWindowReady()) DisableCursor();   // headless harness has no window
    B.hasLastMouse = false;
}

// Read the real devices into battle intent. Windowed play only — the headless
// harness builds BattleInput directly. Mouse-look uses manual position deltas
// (GetMouseDelta is unreliable under WSL/X11).
BattleInput GatherBattleInput() {
    BattleInput in;

    // Deployment (R2): the first windowed gather of a new battle opens the
    // planning pause; SPACE (or LMB) sounds the horn.
    if (B.deployArmed) { B.deployArmed = false; B.deploying = true; }
    if (B.deploying)
        in.beginBattle = IsKeyPressed(KEY_SPACE) ||
                         IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    Vector2 md = { 0, 0 };
    const Vector2 mouse = GetMousePosition();
    if (B.hasLastMouse) {
        md = Vector2Subtract(mouse, B.lastMouse);
        if (Vector2Length(md) > 80.0f) md = { 0, 0 };   // window-focus jump guard
    }
    B.lastMouse = mouse;
    B.hasLastMouse = true;
    if (GetSettings().invertY) md.y = -md.y;
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
    if (IsKeyPressed(KEY_FIVE))  in.formationSelect = 5;   // shield wall (V48)
    if (IsKeyPressed(KEY_F1))    in.order = 1;   // hold position (M2)
    if (IsKeyPressed(KEY_F2))    in.order = 2;   // follow me
    if (IsKeyPressed(KEY_F3))    in.order = 3;   // charge
    if (IsKeyPressed(KEY_Z))     in.mountToggle = true;   // dismount (U11)
    if (IsKeyPressed(KEY_E))     in.kick = true;          // the boot (V33)
    if (IsKeyPressed(KEY_G))     in.pickup = true;        // scavenge (V39)
    if (IsKeyPressed(KEY_N))     in.autoResolve = true;   // fight on paper (V41)
    if (IsKeyPressed(KEY_LEFT_BRACKET))  in.ranksDelta -= 1;
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) in.ranksDelta += 1;
    return in;
}

bool BattleUpdate(const Content& c, float dt, const BattleInput& in, BattleOutcome& out) {
    if (dt > 0.05f) dt = 0.05f;
    g_dbgFrame++;

    // Deployment pause (R2): the field holds its breath while you set the
    // lines — shape (1-4), ranks ([/]), the opening order (F1-F3). You may
    // look around; nothing moves until the horn. Windowed only by
    // construction: headless runs never gather, so B.deploying stays false.
    if (B.deploying) {
        B.yaw   -= in.lookDelta.x * 0.003f;
        B.pitch = Clamp(B.pitch - in.lookDelta.y * 0.003f, -0.4f, 0.6f);
        switch (in.formationSelect) {
            case 1: B.formation = FormationType::Charge;
                    B.order = OrderType::Charge; break;
            case 2: B.formation = FormationType::Line;   break;
            case 3: B.formation = FormationType::Square; break;
            case 4: B.formation = FormationType::Spread; break;
            case 5: B.formation = FormationType::ShieldWall; break;   // V48
            default: break;
        }
        if (in.formationSelect >= 2 && B.order == OrderType::Charge)
            B.order = OrderType::Follow;
        B.ranks += in.ranksDelta;
        if (B.ranks < 1) B.ranks = 1;
        if (B.ranks > 8) B.ranks = 8;
        if (in.order == 1) { B.order = OrderType::Hold; B.holdPos = B.pPos; }
        if (in.order == 2)   B.order = OrderType::Follow;
        if (in.order == 3)   B.order = OrderType::Charge;
        if (in.beginBattle) {
            B.deploying = false;
            B.cryTimer  = 1.6f;
            B.cryText   = "SOUND THE HORN!";
            SfxPlay(Sfx::WarCry);
        }
        if (in.autoResolve) AutoResolve(c);   // fight it on paper (V41)
        return true;   // a held breath: no movement, no arrows, no clocks
    }

    // Headless path (V41): scripts have no deployment pause; the horn's
    // grace period stands in for it.
    if (in.autoResolve && !B.over && B.introTimer > 0 && !B.setup.arena)
        AutoResolve(c);

    // ---------- player intent ----------
    Vector3 fwd = { sinf(B.yaw), 0, cosf(B.yaw) };
    if (!B.over && !B.heroDown) {
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
        bool galloping = false;
        if (Vector3Length(move) > 0) {
            // A horse doubles your pace (identity; numbers TODO(balance)).
            const float speed = (B.mounted ? (B.blocking ? 6.0f : 14.0f)
                                           : (B.blocking ? 3.0f : 7.0f))
                * (1.0f + 0.02f * B.setup.heroAgi);   // Agility (V14)
            B.pPos = Vector3Add(B.pPos, Vector3Scale(Vector3Normalize(move), speed * dt));
            B.walkPhase += dt * 10.0f;
            galloping = B.mounted && !B.blocking;
        }

        // Hooves kick up dust at the gallop.
        if (galloping && (PuffRand() & 3) == 0) SpawnDust(B.pPos);
        if (galloping) SfxPlay(Sfx::Gallop, 0.5f);   // rate limit paces the rhythm

        // Ride enemies down: the hero tramples at the gallop, like cavalry.
        B.pTrampleCd -= dt;
        if (galloping && B.pTrampleCd <= 0) {
            for (Soldier& s : B.soldiers) {
                if (s.hp <= 0 || s.team != Team::Enemy || s.onWall) continue;
                Vector3 d3 = Vector3Subtract(s.pos, B.pPos);
                d3.y = 0;
                if (Vector3LengthSqr(d3) < TRAMPLE_RADIUS * TRAMPLE_RADIUS) {
                    const int wh = B.setup.heroLoadout.get(EquipSlot::Weapon);
                    DamageSoldier(c, s, ApplyArmor(TrampleDamage(c, wh),
                                                   LoadoutArmor(c, TroopLoadout(c, s.troop))));
                    B.pTrampleCd = TRAMPLE_COOLDOWN;
                    B.shake = fminf(1.0f, B.shake + 0.2f);   // the impact carries
                    break;
                }
            }
        }
        B.pPos.x = Clamp(B.pPos.x, -ARENA, ARENA);
        B.pPos.z = Clamp(B.pPos.z, -ARENA, ARENA);
        EnforceWall(B.pPos);

        const float groundY = B.terrain.HeightAt(B.pPos.x, B.pPos.z) +
                              (B.hasWall ? LadderClimbBump(B.pPos.x, B.pPos.z) : 0.0f);
        if (in.jump && B.pPos.y <= groundY + 0.02f) B.vY = 6.0f;
        B.vY -= 18.0f * dt;
        B.pPos.y += B.vY * dt;
        if (B.pPos.y < groundY) { B.pPos.y = groundY; B.vY = 0; }

        // Momentum bookkeeping (T5): how fast the hero actually moves.
        {
            Vector3 dp = Vector3Subtract(B.pPos, B.prevHeroPos);
            dp.y = 0;
            B.heroSpeed   = Vector3Length(dp) / fmaxf(dt, 1e-4f);
            B.prevHeroPos = B.pPos;
        }

        B.blocking = in.block;
        B.cooldown -= dt;
        if (B.swing > 0) B.swing -= dt * 4.0f;

        // ---- dismount / remount (U11): Z, and the horse waits for you ----
        if (in.mountToggle) {
            if (B.mounted) {
                LooseHorse h;
                h.pos = B.pPos;
                h.yaw = B.yaw;
                h.hp  = B.pHorseHp;
                h.target = h.pos;
                h.yours  = true;   // it waits where you left it
                B.looseHorses.push_back(h);
                B.mounted = false;
            } else {
                for (auto it = B.looseHorses.begin();
                     it != B.looseHorses.end(); ++it) {
                    Vector3 d = Vector3Subtract(it->pos, B.pPos);
                    d.y = 0;
                    if (Vector3Length(d) < 3.0f) {   // any horse will carry you
                        B.pHorseHp = it->hp;
                        B.mounted  = true;
                        B.looseHorses.erase(it);
                        SfxPlay(Sfx::Gallop, 0.6f);
                        break;
                    }
                }
            }
        }

        // ---- switch active weapon (a hero may carry several) ----
        if (in.swapWeapon && (int)B.heroArsenal.size() > 1) {
            B.heroWeapon = (B.heroWeapon + 1) % (int)B.heroArsenal.size();
            B.setup.heroLoadout.set(EquipSlot::Weapon, B.heroArsenal[B.heroWeapon]);
        }

        // ---- strategy / formation menu (~ toggles it) ----
        if (in.toggleMenu) B.showMenu = !B.showMenu;
        if (B.showMenu) {
            switch (in.formationSelect) {
                // Picking a shape implies an order (M2): Charge frees the
                // line, any held shape means "form on me".
                case 1: B.formation = FormationType::Charge;
                        B.order = OrderType::Charge; break;
                case 2: B.formation = FormationType::Line;   break;
                case 3: B.formation = FormationType::Square; break;
                case 4: B.formation = FormationType::Spread; break;
                case 5: B.formation = FormationType::ShieldWall; break;   // V48
                default: break;
            }
            if (in.formationSelect >= 2 && B.order == OrderType::Charge)
                B.order = OrderType::Follow;
            B.ranks += in.ranksDelta;
            if (B.ranks < 1) B.ranks = 1;
            if (B.ranks > 8) B.ranks = 8;
        }

        // ---- battlefield orders (M2): barked instantly, no menu ----
        if (in.order != 0) {
            switch (in.order) {
                case 1: B.order = OrderType::Hold; B.holdPos = B.pPos; break;
                case 2: B.order = OrderType::Follow; break;
                default: B.order = OrderType::Charge; break;
            }
            B.cryTimer = 1.6f;   // the banner rings the order
            B.cryText  = OrderName(B.order);
            SfxPlay(Sfx::WarCry);
        }

        // ---- the kick (V33): a short shove, Warband's answer to a turtled
        //      shield — no damage to speak of, but it staggers THROUGH a
        //      guard and opens the man for the real blow. On foot only.
        B.kickCd = fmaxf(0.0f, B.kickCd - dt);
        if (in.kick && B.kickCd <= 0 && !B.mounted && B.pHp > 0) {
            B.kickCd = 1.2f;   // TODO(balance)
            const Vector3 kfwd = { sinf(B.yaw), 0, cosf(B.yaw) };
            for (Soldier& s : B.soldiers) {
                if (s.hp <= 0 || s.escaped || s.team != Team::Enemy) continue;
                Vector3 to = Vector3Subtract(s.pos, B.pPos);
                to.y = 0;
                const float d = Vector3Length(to);
                if (d > 2.2f || d < 0.01f) continue;
                if (Vector3DotProduct(Vector3Normalize(to), kfwd) < 0.5f) continue;
                s.stun  = STUN_TIME * 1.5f;   // through shield and immunity both
                s.swing = 0;
                s.hp   -= 1.0f;               // an insult, not a wound
                s.flash = 1.0f;
                const Vector3 push = Vector3Scale(Vector3Normalize(to), 1.1f);
                s.pos = Vector3Add(s.pos, push);
                B.heroKicksLanded++;
                SfxPlay(Sfx::Thud, 0.8f);
                break;   // one boot, one man
            }
        }

        // ---- scavenge (V39): G over a fallen man takes up his weapon in
        //      place of your active one — the field re-arms the survivor.
        if (in.pickup && B.pHp > 0) {
            for (Soldier& s : B.soldiers) {
                if (s.hp > 0 || s.escaped || s.looted) continue;
                Vector3 to = Vector3Subtract(s.pos, B.pPos);
                to.y = 0;
                if (Vector3Length(to) > 2.5f) continue;
                const Loadout& lo = TroopLoadout(c, s.troop);
                const int w = lo.weaponCount() > 0 ? lo.weaponAt(0) : -1;
                if (!c.weapons.valid(w)) continue;
                s.looted = true;
                B.heroArsenal[B.heroWeapon] = w;   // yours falls where he lies
                B.setup.heroLoadout.set(EquipSlot::Weapon, w);
                B.pickupMsg   = TextFormat("TAKEN UP: %s", c.weapons[w].name.c_str());
                B.pickupTimer = 1.8f;
                SfxPlay(Sfx::Swing, 0.6f);
                break;
            }
        }

        // ---- attack: HOLD LMB to ready a swing in the direction you move the
        //      mouse, then RELEASE to strike (Mount & Blade style) ----
        if (B.blocking) { B.readying = false; B.windup = 0.0f; }   // guarding cancels a wind-up
        if (in.attackPress && B.cooldown <= 0 && !B.blocking) {
            B.readying = true;
            B.windup = 0.0f;
            // Warband rule (U5): the swing direction locks at the moment of
            // the click, read from the last mouse flick — mouse only, and
            // holding just holds. Looking around mid-hold changes nothing.
            if (Vector2Length(B.aimAccum) > 1.5f)
                B.attackDir = DirFromMotion(B.aimAccum);
        }
        if (B.readying)
            B.windup = fminf(1.0f, B.windup + dt * 4.0f);
        if (in.attackRelease && B.readying) {
            B.readying = false;
            B.windup = 0.0f;
            B.swing = 1.0f;
            SfxPlay(Sfx::Swing);
            const int wh = B.setup.heroLoadout.get(EquipSlot::Weapon);
            const float reach = WeaponReach(c, wh);
            B.cooldown = WeaponCooldown(c, wh);
            // The hero hits like a hero: the first user-playtest balance
            // change (T) — with flat 100/10 numbers a player needed ten
            // clean hits per man while three men killed him in five
            // seconds, which read as "my attacks do nothing".
            const float HERO_DAMAGE_FACTOR = 2.5f;   // playtest-tuned 2026-07-21
            // Mounted identity (T5): the saddle adds reach, and the gallop
            // adds weight — up to +50% at full stride. TODO(balance).
            const float mountReach = B.mounted ? 0.9f : 0.0f;
            const float momentum   = (B.mounted
                ? 1.0f + 0.5f * fminf(1.0f, B.heroSpeed / 14.0f) : 1.0f)
                * (1.0f + 0.05f * B.setup.heroStr);   // Strength (V14)
            for (Soldier& s : B.soldiers) {
                if (s.hp <= 0 || s.escaped || s.team != Team::Enemy) continue;
                Vector3 to = Vector3Subtract(s.pos, B.pPos);
                to.y = 0;
                const float d = Vector3Length(to);
                if (d < reach + mountReach + 0.6f && d > 0.01f &&
                    Vector3DotProduct(Vector3Normalize(to), fwd) > 0.4f) {
                    // ~120° frontal arc; armour soaks per hit, and a raised
                    // shield meets the hero's chosen swing direction (G4).
                    const int vi = (int)(&s - &B.soldiers[0]);
                    float dmg = ApplyArmor(WeaponDamage(c, wh) * HERO_DAMAGE_FACTOR
                                               * momentum,
                                           LoadoutArmor(c, TroopLoadout(c, s.troop)));
                    const float before = dmg;
                    dmg = ShieldSoak(c, vi, s, (int)B.attackDir, dmg);
                    if (dmg < before) {
                        SpawnSparks(s.pos);
                        SfxPlay(Sfx::Clang, 0.6f);
                        B.soakFlash = 1.0f;   // name those sparks (U5)
                    }
                    // A landed blow sounds as heavy as it hit (V24): a glance
                    // taps, a plate-cracking overhead lands like a hammer.
                    if (dmg > 0.5f)
                        SfxPlay(Sfx::Thud, Clamp(0.3f + dmg / 25.0f, 0.3f, 1.0f));
                    DamageSoldier(c, s, dmg);
                    if (s.hp <= 0) FreeHorse(c, s);   // (T6)
                    if (s.hp <= 0) {   // a kill by the hero's own hand rallies
                        B.heroKills++;
                        B.rallyPulse = RALLY_PULSE_TIME;
                        SfxPlay(Sfx::WarCry, 0.25f);
                        // The kill-cry stiffens friends and shakes foes (K4).
                        if (!B.setup.arena)
                            B.grid.ForNeighbors(B.pPos, RALLY_RADIUS * 2.0f, [&](int j) {
                                Soldier& w = B.soldiers[j];
                                if (w.hp <= 0) return;
                                w.nerve += (w.team == Team::Player) ? NERVE_RALLY
                                                                    : -NERVE_ALLY_DEATH;
                                w.nerve = fminf(NERVE_MAX, w.nerve);
                            });
                    }
                }
            }
        }
    } else if (B.over) {
        B.overTimer -= dt;
        if (B.overTimer <= 0 && !B.reported) {
            B.reported = true;
            out.won = B.won;
            out.playerLosses = ComputeLosses();
            out.allyLosses   = ComputeAllyLosses();
            out.enemyLosses  = ComputeEnemyLosses();
            // A won field's strays are yours to round up (V22).
            out.horsesTaken = B.won ? (int)B.looseHorses.size() : 0;
            // ...and the men who yielded march in your train (V42).
            out.enemySurrendered.assign(c.troops.size(), 0);
            if (B.won)
                for (int t = 0; t < c.troops.size() && t < (int)B.surrendered.size(); ++t)
                    out.enemySurrendered[t] = B.surrendered[t];
            return false;   // battle over — caller returns to the world map
        }
    }

    // ---------- soldier AI (multithreaded) ----------
    // Orders (M2) resolve to an effective shape and anchor here, so ComputeAI
    // stays order-blind: Charge frees everyone; Follow anchors the shape on
    // the hero (a shapeless Charge formation defaults to Line); Hold freezes
    // the anchor where the order was barked.
    FormationType effFormation = B.formation;
    Vector3       anchor       = B.pPos;
    if (B.order == OrderType::Charge) {
        effFormation = FormationType::Charge;
    } else {
        if (effFormation == FormationType::Charge) effFormation = FormationType::Line;
        if (B.order == OrderType::Hold) anchor = B.holdPos;
    }
    const float   anchorYaw = B.yaw;
    const int     n = (int)B.soldiers.size();
    if (!B.over && n > 0) {
        B.cmds.resize(n);
        // Rebuild the proximity grid and the per-soldier "how many foes aim at
        // me" tallies — the read-only inputs of this tick's target scoring.
        B.grid.Build(B.soldiers);
        B.targeted.assign(n, 0);
        for (const Soldier& s : B.soldiers)
            if (s.hp > 0 && s.target >= 0 && s.target < n) B.targeted[s.target]++;

        // Morale (K4): a soldier whose nerve broke runs; the banner rings
        // once per side when half its strength has fled the field.
        if (!B.setup.arena) {
            int playerFled = 0, enemyFled = 0;
            for (Soldier& s : B.soldiers) {
                if (s.hp <= 0) continue;
                if (!s.routed && !s.onWall && s.nerve <= 0.0f) s.routed = true;
                if (s.routed || s.escaped)
                    (s.team == Team::Enemy ? enemyFled : playerFled)++;
            }
            auto ringBanner = [&](const char* text) {
                B.routBanner = 2.5f;
                B.routText   = text;
                SfxPlay(Sfx::WarCry, 0.4f);
            };
            if (!B.enemySideRouted && B.startEnemySide > 0 &&
                enemyFled * 2 >= B.startEnemySide) {
                B.enemySideRouted = true;
                ringBanner("THE ENEMY BREAKS AND RUNS!");
            }
            if (!B.playerSideRouted && B.startPlayerSide > 0 &&
                playerFled * 2 >= B.startPlayerSide) {
                B.playerSideRouted = true;
                ringBanner("YOUR LINE BREAKS!");
            }

            // Quarter (V42): a side cut below a fifth of its strength with
            // its colours in the mud stops fighting — the rest throw down
            // their arms and pass to the victor's train. TODO(balance).
            if (!B.over && B.startEnemySide >= 5) {
                int aliveE = 0, aliveP = 0;
                for (const Soldier& s : B.soldiers) {
                    if (s.hp <= 0 || s.escaped) continue;
                    (s.team == Team::Enemy ? aliveE : aliveP)++;
                }
                // Quarter comes three ways (TODO(balance) all thresholds):
                // a routed side holds to a fifth, a side with its colours in
                // the mud to a third — and a bloodied remnant (half down)
                // facing six-to-one odds yields on the spot.
                const bool broken  = (B.enemySideRouted || B.bannerIdx[1] < 0) &&
                                     aliveE * (B.bannerIdx[1] < 0 ? 3 : 5) <=
                                         B.startEnemySide;
                const bool hopeless = aliveE * 2 <= B.startEnemySide &&
                                      aliveE * 6 <= aliveP;
                if (aliveE > 0 && (broken || hopeless)) {
                    if (B.surrendered.empty())
                        B.surrendered.assign(256, 0);   // per-troop, roomy
                    for (Soldier& s : B.soldiers)
                        if (s.hp > 0 && !s.escaped && s.team == Team::Enemy) {
                            s.escaped = true;   // off the field, but not free
                            if (s.troop >= 0 && s.troop < (int)B.surrendered.size())
                                B.surrendered[s.troop]++;
                        }
                    B.routBanner = 2.5f;
                    B.routText   = "THEY THROW DOWN THEIR ARMS!";
                    SfxPlay(Sfx::Fanfare, 0.7f);
                }
            }

            // The standard falls (V32): the whole side feels it, and the
            // nearest living hand takes up the colours.
            for (int side = 0; side < 2; ++side) {
                const int bi = B.bannerIdx[side];
                if (bi < 0 || B.soldiers[bi].hp > 0) continue;
                const Team team = side == 1 ? Team::Enemy : Team::Player;
                for (Soldier& w : B.soldiers)
                    if (w.hp > 0 && w.team == team) w.nerve -= NERVE_ALLY_DEATH;
                B.bannerFlash    = 2.2f;
                B.bannerFellOurs = side == 0;
                SfxPlay(Sfx::Knell, 0.5f);
                int   next = -1; float bestD = 1e9f;
                for (int j = 0; j < n; ++j) {
                    const Soldier& w = B.soldiers[j];
                    if (w.hp <= 0 || w.team != team || w.routed) continue;
                    const float d = Vector3DistanceSqr(w.pos, B.soldiers[bi].pos);
                    if (d < bestD) { bestD = d; next = j; }
                }
                B.bannerIdx[side] = next;   // -1: the colours lie in the mud
            }
        }

        // A holding line breaks all at once: the first foe to close within
        // reach of any of them sends the whole army forward with a roar.
        if (B.enemyHoldsLine && !B.enemyCharged) {
            constexpr float BREAK_DIST = 28.0f;
            for (int i = 0; i < n && !B.enemyCharged; ++i) {
                const Soldier& e = B.soldiers[i];
                if (e.hp <= 0 || e.team != Team::Enemy) continue;
                if (Vector3DistanceSqr(e.pos, B.pPos) < BREAK_DIST * BREAK_DIST) {
                    B.enemyCharged = true;
                    break;
                }
                B.grid.ForNeighbors(e.pos, BREAK_DIST, [&](int j) {
                    const Soldier& p = B.soldiers[j];
                    if (p.team != Team::Player) return;
                    if (Vector3DistanceSqr(e.pos, p.pos) < BREAK_DIST * BREAK_DIST)
                        B.enemyCharged = true;
                });
            }
            if (B.enemyCharged) {
                B.cryTimer = 2.2f;
                B.cryText  = "THEY CHARGE!";
                SfxPlay(Sfx::WarCry);
            }
        }

        // Phase 1 — compute every soldier's intent in parallel from a read-only
        // snapshot. Nothing is mutated here, so there are no data races.
        ThreadPool::Global().For(0, n, 24, [&](int i) {
            if (B.soldiers[i].hp > 0 && !B.soldiers[i].escaped)
                B.cmds[i] = ComputeAI(c, i, dt, effFormation, B.ranks, anchor, anchorYaw, B.ownCount);
        });
        // Phase 2 — apply movement/state serially, accumulate damage, then deal it.
        B.dmg.assign(n, 0.0f);
        float playerDamage = 0.0f;
        for (int i = 0; i < n; ++i) {
            Soldier& s = B.soldiers[i];
            if (s.hp <= 0 || s.escaped) continue;
            if (s.routed) {
                s.routTime += dt;
                if (s.routTime > ROUT_ESCAPE_TIME) { s.escaped = true; continue; }
            }
            // Reeling from a blow (T): a stunned soldier neither moves nor
            // swings — the opening that blocking and swing-craft buy you.
            s.stunImmune -= dt;
            if (s.stun > 0) {
                s.stun -= dt;
                s.cooldown = fmaxf(s.cooldown, 0.2f);   // no instant riposte
                continue;
            }

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
            if (IsMounted(c, s) && Vector3LengthSqr(cmd.step) > 0.0001f &&
                (PuffRand() & 7) == 0)
                SpawnDust(s.pos);
            if (IsMounted(c, s) && s.trampleCd <= 0 &&
                Vector3Length(cmd.step) > 0.5f * c.troops[s.troop].moveSpeed * dt) {
                // Grid lookup; positions moved a little since the build, but a
                // trample radius is far coarser than one frame of drift.
                B.grid.ForNeighbors(s.pos, TRAMPLE_RADIUS + 1.0f, [&](int j) {
                    Soldier& o = B.soldiers[j];
                    if (s.trampleCd > 0 || o.hp <= 0 || o.team == s.team || o.onWall)
                        return;
                    Vector3 d3 = Vector3Subtract(o.pos, s.pos);
                    d3.y = 0;
                    if (Vector3LengthSqr(d3) < TRAMPLE_RADIUS * TRAMPLE_RADIUS) {
                        B.dmg[j] += ApplyArmor(TrampleDamage(c, s.activeWeapon),
                                               LoadoutArmor(c, TroopLoadout(c, o.troop)));
                        s.trampleCd = TRAMPLE_COOLDOWN;
                    }
                });
            }
            // Armour soaks per hit, so reduction happens per blow, not per frame.
            if (cmd.hitSoldier >= 0)
                B.dmg[cmd.hitSoldier] += ShieldSoak(
                    c, cmd.hitSoldier, B.soldiers[cmd.hitSoldier], cmd.swingDir,
                    ApplyArmor(cmd.hitDamage,
                               LoadoutArmor(c, TroopLoadout(c, B.soldiers[cmd.hitSoldier].troop))));
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
                SfxPlay(Sfx::Loose,
                        Clamp(1.0f - Vector3Distance(s.pos, B.pPos) / 45.0f, 0.05f, 0.8f));
            }
        }
        // The press of bodies (U6): men cannot share ground. One positional
        // relaxation per frame — grid-accelerated, half-push each — keeps
        // crowds honest without a solver. TODO(balance): the gap.
        {
            constexpr float BODY_GAP = 0.9f;
            for (int i = 0; i < n; ++i) {
                Soldier& s = B.soldiers[i];
                if (s.hp <= 0 || s.escaped || s.onWall) continue;
                B.grid.ForNeighbors(s.pos, BODY_GAP + 0.6f, [&](int j) {
                    if (j <= i) return;
                    Soldier& o = B.soldiers[j];
                    if (o.hp <= 0 || o.escaped || o.onWall) return;
                    Vector3 d = Vector3Subtract(o.pos, s.pos);
                    d.y = 0;
                    const float dist = Vector3Length(d);
                    if (dist >= BODY_GAP) return;
                    if (dist < 1e-4f) {   // perfect overlap: pick a direction
                        o.pos.x += (j & 1) ? 0.06f : -0.06f;
                        return;
                    }
                    const Vector3 pd =
                        Vector3Scale(d, (BODY_GAP - dist) * 0.5f / dist);
                    s.pos = Vector3Subtract(s.pos, pd);
                    o.pos = Vector3Add(o.pos, pd);
                });
                EnforceWall(s.pos);
            }
        }

        for (int i = 0; i < n; ++i) {
            Soldier& s = B.soldiers[i];
            if (s.hp <= 0) continue;
            s.flash = fmaxf(0.0f, s.flash - dt * 5.0f);
            s.nerve = fminf(NERVE_MAX, s.nerve + NERVE_REGEN * dt);   // courage returns
            if (B.dmg[i] > 0.0f) {
                DamageSoldier(c, s, B.dmg[i]);   // horse soaks its share
                if (s.hp <= 0) FreeHorse(c, s);  // the mount outlives him (T6)
                // A death every witness feels (K4): friends flinch, foes cheer.
                if (s.hp <= 0 && !B.setup.arena)
                    B.grid.ForNeighbors(s.pos, NERVE_WITNESS_R, [&](int j) {
                        Soldier& w = B.soldiers[j];
                        if (j == i || w.hp <= 0) return;
                        w.nerve += (w.team == s.team) ? -NERVE_ALLY_DEATH
                                                      : NERVE_ENEMY_DEATH;
                        w.nerve = fminf(NERVE_MAX, w.nerve);
                    });
            }
        }
        B.pFlash     = fmaxf(0.0f, B.pFlash - dt * 5.0f);
        B.rallyPulse = fmaxf(0.0f, B.rallyPulse - dt);
        if (playerDamage > 0.0f && B.pHp > 0) {
            float d = B.blocking ? playerDamage * BLOCK_MELEE_FACTOR : playerDamage;
            if (B.mounted) {   // the horse soaks its share — and can fall
                const float toHorse = d * HORSE_HIT_SHARE;
                B.pHorseHp -= toHorse;
                d -= toHorse;
                if (B.pHorseHp <= 0) B.mounted = false;
            }
            B.pHp -= d;
            B.pFlash = 1.0f;
            if (B.blocking) SpawnSparks(B.pPos);   // steel meets shield
            else            SpawnBlood(B.pPos);
            B.shake = fminf(1.0f, B.shake + (B.blocking ? 0.25f : 0.6f));
            SfxPlay(B.blocking ? Sfx::Clang : Sfx::Thud);
        }

        // Keep living soldiers sitting on the terrain surface (they moved in
        // x/z) — except wall posts, whose feet stay on the rampart.
        for (Soldier& s : B.soldiers)
            if (s.hp > 0 && !s.onWall)
                s.pos.y = B.terrain.HeightAt(s.pos.x, s.pos.z) +
                          (B.hasWall ? LadderClimbBump(s.pos.x, s.pos.z) : 0.0f);

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
            // Hit a soldier of the opposing team? A shield-bearer takes shafts
            // on the wood while it lasts (G4).
            for (Soldier& s : B.soldiers) {
                if (s.hp <= 0 || s.escaped || s.team == a.team) continue;
                const Vector3 chest = Vector3Add(s.pos, { 0, 1.2f, 0 });
                if (Vector3DistanceSqr(a.pos, chest) < ARROW_HIT_RADIUS * ARROW_HIT_RADIUS) {
                    float dmg = ApplyArmor(a.damage,
                                           LoadoutArmor(c, TroopLoadout(c, s.troop)));
                    if (HasShield(c, s) && s.shieldHp > 0) {
                        dmg *= BLOCK_MISSILE_FACTOR;
                        s.shieldHp -= SHIELD_WEAR_PER_HIT;
                    }
                    DamageSoldier(c, s, dmg);
                    if (s.hp <= 0) FreeHorse(c, s);   // (T6)
                    a.alive = false;
                    break;
                }
            }
            // Hit the player?
            if (a.alive && a.team == Team::Enemy && B.pHp > 0) {
                const Vector3 chest = Vector3Add(B.pPos, { 0, 1.2f, 0 });
                if (Vector3DistanceSqr(a.pos, chest) < ARROW_HIT_RADIUS * ARROW_HIT_RADIUS) {
                    float d = ApplyArmor(a.damage, LoadoutArmor(c, B.setup.heroLoadout));
                    if (B.blocking) d *= BLOCK_MISSILE_FACTOR;
                    if (B.mounted) {
                        const float toHorse = d * HORSE_HIT_SHARE;
                        B.pHorseHp -= toHorse;
                        d -= toHorse;
                        if (B.pHorseHp <= 0) B.mounted = false;
                    }
                    B.pHp -= d;
                    B.pFlash = 1.0f;
                    if (B.blocking) {
                        SpawnSparks(B.pPos);
                        B.parryFlash = 0.9f;   // name the sparks on YOU (U5)
                    } else {
                        SpawnBlood(B.pPos);
                    }
                    B.shake = fminf(1.0f, B.shake + 0.35f);
                    SfxPlay(B.blocking ? Sfx::Clang : Sfx::Thud);
                    a.alive = false;
                }
            }
        }
        B.arrows.erase(std::remove_if(B.arrows.begin(), B.arrows.end(),
                                      [](const Arrow& a) { return !a.alive; }),
                       B.arrows.end());

        // ---------- masterless horses (T6) ----------
        // They wander, shy from the nearest fighter, and keep off the walls.
        for (LooseHorse& h : B.looseHorses) {
            if (h.yours) continue;   // your horse waits faithfully (U11)
            h.wanderT -= dt;
            if (h.wanderT <= 0) {
                h.wanderT = 2.0f + (float)(PuffRand() % 300) / 100.0f;
                const float a = (float)(PuffRand() % 628) / 100.0f;
                h.target = Vector3Add(h.pos, { sinf(a) * 14.0f, 0, cosf(a) * 14.0f });
            }
            // Shy of the press: drift away from close fighting men.
            B.grid.ForNeighbors(h.pos, 5.0f, [&](int j) {
                const Soldier& s = B.soldiers[j];
                if (s.hp <= 0) return;
                Vector3 away = Vector3Subtract(h.pos, s.pos);
                away.y = 0;
                if (Vector3Length(away) > 0.1f)
                    h.target = Vector3Add(h.pos, Vector3Scale(
                        Vector3Normalize(away), 16.0f));
            });
            Vector3 to = Vector3Subtract(h.target, h.pos);
            to.y = 0;
            if (Vector3Length(to) > 0.6f) {
                h.yaw = atan2f(to.x, to.z);
                h.pos = Vector3Add(h.pos,
                                   Vector3Scale(Vector3Normalize(to), 4.5f * dt));
                h.walkPhase += dt * 6.0f;
            }
            EnforceWall(h.pos);
            h.pos.y = B.terrain.HeightAt(h.pos.x, h.pos.z);
        }

        // blood puffs drift and fade
        for (auto& p : B.puffs) {
            p.life -= dt;
            p.vel.y -= 9.0f * dt;
            p.pos = Vector3Add(p.pos, Vector3Scale(p.vel, dt));
        }
        B.puffs.erase(std::remove_if(B.puffs.begin(), B.puffs.end(),
                                     [](const BattleState::Puff& p) { return p.life <= 0; }),
                      B.puffs.end());
    }

    // Tallies for the HUD and win/lose, computed after damage is applied.
    // Only soldiers still willing to fight count toward the HUD and the win
    // check — the routed are running, the escaped are gone (but alive: they
    // are survivors, not casualties, in the outcome).
    B.aliveAllies = 0;
    B.aliveEnemies = 0;
    for (const Soldier& s : B.soldiers) {
        if (s.hp <= 0 || s.routed || s.escaped) continue;
        (s.team == Team::Enemy ? B.aliveEnemies : B.aliveAllies)++;
    }

    // ---------- win / lose ----------
    // A fallen hero is knocked senseless, not beaten: the warband fights on
    // and the field decides. Defeat comes only when no one is left standing.
    if (!B.over) {
        if (B.pHp <= 0 && !B.heroDown) {
            B.heroDown = true;
            SfxPlay(Sfx::Knell);
            // The banner falls and the whole line feels it (K4).
            if (!B.setup.arena)
                for (Soldier& s : B.soldiers)
                    if (s.hp > 0 && s.team == Team::Player)
                        s.nerve -= NERVE_HERO_DOWN;
        }
        if (B.aliveEnemies == 0)                      EndBattle(!B.heroDown || B.aliveAllies > 0);
        else if (B.heroDown && B.aliveAllies == 0)    EndBattle(false);
    }
    return true;
}

void BattleDraw(const Content& c) {
    // Field ambience swells with the size of the fight still standing.
    SfxAmbience(0.12f + 0.4f * fminf((B.aliveAllies + B.aliveEnemies) / 250.0f, 1.0f));
    SfxRain(B.raining ? 0.35f : 0.0f);   // the patter sits under the field din

    // ---------- camera ----------
    B.shake = fmaxf(0.0f, B.shake - GetFrameTime() * 3.0f);
    Camera3D cam = { 0 };
    const Vector3 look = { sinf(B.yaw) * cosf(B.pitch), sinf(B.pitch), cosf(B.yaw) * cosf(B.pitch) };
    const float eyeUp = B.mounted ? 3.25f : 2.0f;   // taller in the saddle
    Vector3 eye = { B.pPos.x, B.pPos.y + eyeUp, B.pPos.z };
    if (B.shake > 0.01f) {   // a struck helmet rings
        const unsigned int h1 = PuffRand(), h2 = PuffRand();
        eye.x += (((h1 & 0xFF) / 255.0f) - 0.5f) * 0.30f * B.shake;
        eye.y += (((h2 & 0xFF) / 255.0f) - 0.5f) * 0.30f * B.shake;
    }
    cam.position = Vector3Subtract(eye, Vector3Scale(look, 6.0f));
    cam.position.y = fmaxf(cam.position.y, 0.5f);
    cam.target = Vector3Add(eye, Vector3Scale(look, 4.0f));
    cam.up = { 0, 1, 0 };
    cam.fovy = 60;
    cam.projection = CAMERA_PERSPECTIVE;

    // Frustum cull (V40): anything meaningfully behind the camera never
    // reaches the GPU. A wide margin (dot < -0.25, and never within 12u)
    // keeps shoulders and swing trails from popping at the frame edge.
    const Vector3 camFwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    auto BehindCamera = [&](Vector3 p) {
        const Vector3 to = Vector3Subtract(p, cam.position);
        if (Vector3LengthSqr(to) < 144.0f) return false;   // 12u safety bubble
        return Vector3DotProduct(Vector3Normalize(to), camFwd) < -0.25f;
    };

    // ================= DRAW =================
    BeginDrawing();
    // The sky follows the campaign clock (O3): blue noon, amber dusk,
    // deep night. Also clears the DEPTH buffer.
    const float tod   = B.setup.timeOfDay;
    const bool  night = tod >= 0.82f || tod < 0.06f;
    Color sky = { 92, 148, 214, 255 };
    if (night)              sky = { 26, 34, 60, 255 };
    else if (tod >= 0.70f)  sky = { 178, 116, 84, 255 };   // dusk
    else if (tod < 0.10f)   sky = { 148, 120, 104, 255 };  // dawn
    ClearBackground(sky);
    // A real sky (V4): zenith deepens, horizon pales — drawn flat before
    // the 3D pass, which paints over it with depth. Costs one rectangle.
    {
        Color zen = { (unsigned char)(sky.r * 0.55f),
                      (unsigned char)(sky.g * 0.60f),
                      (unsigned char)(sky.b * 0.75f), 255 };
        Color hor = { (unsigned char)fminf(255, sky.r * 1.25f + 18),
                      (unsigned char)fminf(255, sky.g * 1.18f + 14),
                      (unsigned char)fminf(255, sky.b * 1.08f + 8), 255 };
        DrawRectangleGradientV(0, 0, GetScreenWidth(),
                               GetScreenHeight() * 2 / 3, zen, hor);
        DrawRectangle(0, GetScreenHeight() * 2 / 3, GetScreenWidth(),
                      GetScreenHeight() / 3, hor);
    }
    // Sky: gradient with a low sun — or a leaden overcast when it rains.
    if (B.raining) {
        DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(),
                               Color{ 96, 104, 120, 255 }, Color{ 150, 156, 166, 255 });
    } else if (night) {
        // A true night sky (V45): the old unconditional day gradient was
        // painting noon over midnight. Stars in fixed hashed places, and a
        // moon where the sun would stand.
        DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(),
                               Color{ 10, 14, 30, 255 }, Color{ 36, 44, 72, 255 });
        const int w = GetScreenWidth(), h = GetScreenHeight();
        for (int i = 0; i < 140; ++i) {
            unsigned int s = (unsigned)i * 2654435761u;
            s ^= s >> 13; s *= 0x5bd1e995u; s ^= s >> 15;
            const int sx = (int)(s % (unsigned)w);
            const int sy = (int)((s >> 11) % (unsigned)(h * 2 / 3));
            const float tw = 0.35f + 0.65f * (((s >> 22) & 0xFF) / 255.0f);
            DrawRectangle(sx, sy, ((s >> 7) & 3) == 0 ? 2 : 1, 1,
                          Fade(RAYWHITE, tw));
        }
        DrawCircleGradient(w * 3 / 4, h / 4, 70,
                           Fade(Color{ 224, 228, 240, 255 }, 0.95f),
                           Fade(Color{ 224, 228, 240, 255 }, 0.0f));
        DrawCircle(w * 3 / 4, h / 4, 26, Color{ 226, 230, 240, 255 });
        DrawCircle(w * 3 / 4 + 9, h / 4 - 4, 22, Color{ 24, 30, 52, 255 });   // crescent
    } else {
        Color top = { 92, 148, 214, 255 }, bot = { 208, 224, 238, 255 };
        if (tod >= 0.70f) { top = { 150, 96, 84, 255 };  bot = { 236, 168, 120, 255 }; }
        else if (tod < 0.10f) { top = { 128, 108, 110, 255 }; bot = { 232, 196, 160, 255 }; }
        DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), top, bot);
        DrawCircleGradient(GetScreenWidth() * 3 / 4, GetScreenHeight() / 4, 90,
                           Fade(Color{ 255, 244, 214, 255 }, 0.9f), Fade(WHITE, 0.0f));
    }

    BeginMode3D(cam);
    BeginShaderMode(GetLitShader());   // one sun over everything solid
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
        // Climbing points (N1): ladders leaning on the outer face; a built
        // siege tower is a rolling timber mass with its ramp on the rampart.
        for (const ClimbPoint& cp : g_climbs) {
            const float lx   = cp.x;
            const float gy   = B.terrain.HeightAt(lx, WALL_Z);
            const Color wood = { 122, 88, 54, 255 };
            if (cp.tower) {
                DrawCube({ lx, gy + (WALL_HEIGHT + 1.6f) * 0.5f, WALL_Z - 3.2f },
                         TOWER_HALF * 2.0f, WALL_HEIGHT + 1.6f, 3.4f,
                         Color{ 96, 70, 44, 255 });
                DrawCube({ lx, gy + WALL_HEIGHT + 0.3f, WALL_Z - 1.0f },
                         TOWER_HALF * 1.6f, 0.3f, 2.6f, wood);   // the ramp
                continue;
            }
            const Vector3 base{ lx, gy, WALL_Z - 2.6f };
            const Vector3 top { lx, gy + WALL_HEIGHT + 0.6f, WALL_Z - 0.6f };
            for (const float rx : { -0.7f, 0.7f })
                DrawCapsule({ base.x + rx, base.y, base.z },
                            { top.x + rx, top.y, top.z }, 0.10f, 6, 3, wood);
            for (int r = 1; r <= 6; ++r) {
                const float t = (float)r / 7.0f;
                const Vector3 m = Vector3Lerp(base, top, t);
                DrawCube(m, 1.5f, 0.09f, 0.09f, wood);
            }
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
    const float LOD_DIST    = GetSettings().lodDistance;   // player-tunable (J4)
    const float LOD_DIST_SQ = LOD_DIST * LOD_DIST;

    for (const Soldier& s : B.soldiers) {
        if (s.escaped) continue;   // off the field, alive
        if (BehindCamera(s.pos)) continue;   // never reaches the GPU (V40)
        if (s.hp <= 0) {
            const float gy = B.terrain.HeightAt(s.pos.x, s.pos.z);
            DrawCylinder({ s.pos.x, gy + 0.02f, s.pos.z }, 0.9f, 0.9f, 0.02f, 10,
                         Fade(Color{ 110, 20, 20, 255 }, 0.5f));   // blood pool
            DrawCube({ s.pos.x, gy + 0.15f, s.pos.z }, 1.4f, 0.3f, 0.6f, Fade(DARKGRAY, 0.8f));
            DrawSphere({ s.pos.x + 0.8f, gy + 0.16f, s.pos.z }, 0.2f,
                       Color{ 214, 176, 142, 255 });   // a fallen man, not a crate
            continue;
        }
        BlobShadow(B.terrain, s.pos.x, s.pos.z, IsMounted(c, s) ? 0.85f : 0.5f);
        if (Vector3DistanceSqr(cam.position, s.pos) > LOD_DIST_SQ) {
            const Color tint = TeamTint(s.team);
            DrawCube({ s.pos.x, s.pos.y + 0.95f, s.pos.z }, 0.7f, 1.5f, 0.45f, tint);
            DrawCube({ s.pos.x, s.pos.y + 1.85f, s.pos.z }, 0.32f, 0.32f, 0.32f,
                     Color{ 224, 188, 150, 255 });
            const float fracFar = s.hp / s.maxHp;
            if (fracFar < 0.999f)   // only the wounded show a bar
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
        if (IsMounted(c, s)) {
            DrawHorse(s.pos, s.yaw, s.walkPhase);
            riderPos.y += 1.25f;
            pose.walkPhase = 0;   // the rider sits; the horse does the running
        }
        // Implicit health (U2): no floating bars — a hurt man reads from his
        // body, darkening and bloodying as the fight wears him down.
        const float hf = Clamp(s.hp / s.maxHp, 0.0f, 1.0f);
        Color tint = TeamTint(s.team);
        const float dim = 0.45f + 0.55f * hf;
        tint.r = (unsigned char)fminf(255.0f, tint.r * dim + 70.0f * (1.0f - hf));
        tint.g = (unsigned char)(tint.g * dim);
        tint.b = (unsigned char)(tint.b * dim);
        DrawCharacter(c, riderPos, TroopLoadout(c, s.troop), pose, tint);
    }

    // The standards (V32): a tall pole and pennant over each bannerman,
    // drawn at any distance — the line reads from across the field.
    for (int side = 0; side < 2; ++side) {
        const int bi = B.bannerIdx[side];
        if (bi < 0 || bi >= (int)B.soldiers.size()) continue;
        const Soldier& s = B.soldiers[bi];
        if (s.hp <= 0 || s.escaped) continue;
        const Color col = TeamTint(s.team);
        const float bob = sinf(s.walkPhase) * 0.1f;
        DrawCube({ s.pos.x, s.pos.y + 3.2f + bob, s.pos.z }, 0.10f, 4.4f, 0.10f,
                 Color{ 90, 70, 50, 255 });
        DrawCube({ s.pos.x + 0.75f, s.pos.y + 4.9f + bob, s.pos.z }, 1.4f, 0.9f, 0.06f,
                 col);
        DrawCube({ s.pos.x + 0.75f, s.pos.y + 4.42f + bob, s.pos.z }, 1.4f, 0.06f, 0.08f,
                 Fade(BLACK, 0.4f));
    }

    // Where men fell (V12): dark stains, flat on the ground, all battle long.
    for (const Vector3& st : B.stains) {
        if (BehindCamera(st)) continue;   // (V40)
        DrawCylinder({ st.x, B.terrain.HeightAt(st.x, st.z) + 0.02f, st.z },
                     0.7f, 0.9f, 0.015f, 8, Color{ 92, 24, 20, 200 });
    }

    // Ground dressing (V7): drawn only within the lodDistance setting of
    // the hero — the far field stays cheap.
    {
        const float lod = GetSettings().lodDistance;
        for (const Vector4& p : B.props) {
            const float dx = p.x - B.pPos.x, dz = p.z - B.pPos.z;
            if (dx * dx + dz * dz > lod * lod) continue;
            if (BehindCamera({ p.x, p.y, p.z })) continue;   // (V40)
            if (p.w < 0.5f) {   // a tuft: two crossed blades
                DrawCube({ p.x, p.y + 0.22f, p.z }, 0.5f, 0.44f, 0.05f,
                         Color{ 64, 96, 48, 255 });
                DrawCube({ p.x, p.y + 0.22f, p.z }, 0.05f, 0.44f, 0.5f,
                         Color{ 72, 104, 52, 255 });
            } else {            // a stone
                DrawCube({ p.x, p.y + 0.16f, p.z }, 0.5f, 0.34f, 0.4f,
                         Color{ 122, 118, 112, 255 });
            }
        }
    }

    // Masterless horses (T6): riderless, wandering, nobody's to command —
    // except yours (U11), marked so you can find your way back to it.
    for (const LooseHorse& h : B.looseHorses) {
        BlobShadow(B.terrain, h.pos.x, h.pos.z, 0.85f);
        DrawHorse(h.pos, h.yaw, h.walkPhase);
        if (h.yours)
            DrawCylinder({ h.pos.x, h.pos.y + 2.6f, h.pos.z }, 0.0f, 0.25f,
                         0.5f, 6, GOLD);   // a little pennant point
    }

    // particles (blood, dust)
    for (const auto& p : B.puffs)
        DrawCube(p.pos, 0.13f, 0.13f, 0.13f, Fade(p.col, p.life / p.maxLife));

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
    if (!B.heroDown) {
        BlobShadow(B.terrain, B.pPos.x, B.pPos.z, B.mounted ? 0.85f : 0.5f);
        Vector3 heroDraw = B.pPos;
        if (B.mounted) {
            DrawHorse(B.pPos, B.yaw, B.walkPhase);
            // Your horse wears your colour (V17): a caparison so the
            // saddle is unmistakable at a glance.
            const Vector3 hfwd = { sinf(B.yaw), 0, cosf(B.yaw) };
            DrawCube({ B.pPos.x - hfwd.x * 0.1f, B.pPos.y + 0.95f,
                       B.pPos.z - hfwd.z * 0.1f },
                     0.9f + fabsf(hfwd.x) * 0.8f, 0.5f,
                     0.9f + fabsf(hfwd.z) * 0.8f,
                     Color{ 40, 120, 255, 255 });
            heroDraw.y += 1.25f;
            ppose.walkPhase = 0;
        }
        DrawCharacter(c, heroDraw, B.setup.heroLoadout, ppose, Color{ 40, 120, 255, 255 });
    }
    EndShaderMode();

    EndMode3D();

    // Night presses close on the field (O3): a veil over the scene, under
    // the HUD, so torches-and-steel reads without hiding the interface.
    if (night)
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                      Fade(Color{ 10, 16, 40, 255 }, 0.28f));
    else if (tod >= 0.70f && tod < 0.82f)
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                      Fade(Color{ 200, 120, 60, 255 }, 0.10f));

    // Your swing met his shield (U5): say so, and say what beats it —
    // those gold sparks finally have a name.
    B.parryFlash = fmaxf(0.0f, B.parryFlash - GetFrameTime());
    if (B.parryFlash > 0) {
        const char* tb = "BLOCKED";
        ui::Text(tb, (GetScreenWidth() - ui::Measure(tb, 24)) / 2, 96, 24,
                 Fade(SKYBLUE, fminf(1.0f, B.parryFlash * 2.0f)));
    }
    B.soakFlash = fmaxf(0.0f, B.soakFlash - GetFrameTime());
    if (B.soakFlash > 0) {
        const char* t1 = "SHIELD!  vary your swing direction";
        const int w1 = ui::Measure(t1, 22);
        ui::Text(t1, (GetScreenWidth() - w1) / 2, 70, 22,
                 Fade(GOLD, fminf(1.0f, B.soakFlash * 2.0f)));
    }

    // The man in your sights (U2): one flat 2D bar, for him alone — the
    // enemy nearest your crosshair line within striking conversation.
    {
        const Vector3 look = { sinf(B.yaw), 0, cosf(B.yaw) };
        int aim = -1;
        float bestDot = 0.90f;
        for (int i = 0; i < (int)B.soldiers.size(); ++i) {
            const Soldier& s = B.soldiers[i];
            if (s.hp <= 0 || s.escaped || s.team != Team::Enemy) continue;
            Vector3 to = Vector3Subtract(s.pos, B.pPos);
            to.y = 0;
            const float d = Vector3Length(to);
            if (d < 0.5f || d > 16.0f) continue;
            const float dot = Vector3DotProduct(Vector3Scale(to, 1.0f / d), look);
            if (dot > bestDot) { bestDot = dot; aim = i; }
        }
        if (aim >= 0) {
            const Soldier& s = B.soldiers[aim];
            const float hf = Clamp(s.hp / s.maxHp, 0.0f, 1.0f);
            const int bw = 240, bx = (GetScreenWidth() - bw) / 2, by = 46;
            DrawRectangle(bx - 2, by - 2, bw + 4, 14, Fade(BLACK, 0.6f));
            DrawRectangle(bx, by, (int)(bw * hf), 10,
                          hf > 0.5f ? Color{ 200, 60, 50, 255 }
                                    : Color{ 130, 30, 25, 255 });
            DrawRectangleLines(bx - 2, by - 2, bw + 4, 14, Fade(RAYWHITE, 0.4f));
            const char* nm = c.troops.valid(s.troop)
                                 ? c.troops[s.troop].name.c_str() : "?";
            ui::Text(nm, bx, by - 20, 16, Fade(RAYWHITE, 0.85f));
        }
    }

    // ---------- HUD ----------
    DrawRectangle(16, GetScreenHeight() - 46, 306, 28, Fade(BLACK, 0.55f));
    DrawRectangleLines(16, GetScreenHeight() - 46, 306, 28, Fade(GOLD, 0.5f));
    DrawRectangleGradientH(20, GetScreenHeight() - 42,
                           (int)(298 * fmaxf(B.pHp, 0) / B.pMaxHp), 20,
                           Color{ 150, 24, 24, 255 }, Color{ 220, 60, 40, 255 });
    ui::Text("HP", 26, GetScreenHeight() - 41, 18, RAYWHITE);
    if (B.mounted)
        ui::Text(TextFormat("Horse %d", (int)fmaxf(B.pHorseHp, 0)),
                 250, GetScreenHeight() - 41, 16, Fade(RAYWHITE, 0.85f));
    ui::Text(TextFormat("Allies: %d   Enemies: %d   Your kills: %d",
                        B.aliveAllies, B.aliveEnemies, B.heroKills), 18, 12, 22, RAYWHITE);
    if (B.rallyPulse > 0)
        ui::Text("RALLIED", 18, 38, 20, Fade(GOLD, fminf(1.0f, B.rallyPulse)));
    ui::Text("Hold LMB to ready a swing, release to strike | RMB block | Q swap weapon | ~ strategy",
             18, 38, 16, Fade(RAYWHITE, 0.7f));

    const char* dirName[] = { "UP", "DOWN", "LEFT", "RIGHT" };
    const int hwh = B.setup.heroLoadout.get(EquipSlot::Weapon);
    const char* wname = c.weapons.valid(hwh) ? c.weapons[hwh].name.c_str() : "Unarmed";
    ui::Text(TextFormat("Weapon: %s    Order: %s [F1-F3]    Shape: %s (ranks %d)%s",
                        wname, OrderName(B.order), FormationName(B.formation),
                        B.ranks,
                        B.mounted ? "    [Z] dismount"
                        : B.pHorseHp > 0 ? "    [Z] mount (near a horse)" : ""),
                 18, 60, 16, GOLD);
    if (B.deploying) {   // the planning pause (R2)
        const char* d1 = "DEPLOYMENT";
        const int w1 = ui::MeasureTitle(d1, 48);
        DrawRectangle(0, GetScreenHeight() / 3 - 14,
                      GetScreenWidth(), 110, Fade(BLACK, 0.55f));
        ui::Title(d1, (GetScreenWidth() - w1) / 2, GetScreenHeight() / 3, 48, GOLD);
        const char* d2 = "[1-5] shape   [ / ] ranks   [F1-F3] first order   "
                         "SPACE sounds the horn   [N] send them in without you";
        ui::Text(d2, (GetScreenWidth() - ui::Measure(d2, 20)) / 2,
                 GetScreenHeight() / 3 + 62, 20, RAYWHITE);
    }
    if (B.readying)
        ui::Text(TextFormat("Readying swing: %s  (release!)", dirName[(int)B.attackDir]),
                 18, 82, 16, ORANGE);

    DrawLine(GetScreenWidth() / 2 - 8, GetScreenHeight() / 2, GetScreenWidth() / 2 + 8, GetScreenHeight() / 2, RAYWHITE);
    DrawLine(GetScreenWidth() / 2, GetScreenHeight() / 2 - 8, GetScreenWidth() / 2, GetScreenHeight() / 2 + 8, RAYWHITE);

    // Rain: screen-space streaks drifting with time.
    if (B.raining) {
        const float t = (float)GetTime();
        const int w = GetScreenWidth(), h = GetScreenHeight();
        for (int i = 0; i < 170; ++i) {
            unsigned int hh = (unsigned)i * 2654435761u;
            hh ^= hh >> 15;
            const float x = (float)(hh % (unsigned)w);
            const float speed = 620.0f + (hh & 0xFF);
            const float y = fmodf((float)((hh >> 8) % (unsigned)h) + t * speed, (float)(h + 40)) - 20.0f;
            DrawLineEx({ x, y }, { x - 4, y + 16 }, 1.2f, Fade(Color{ 190, 205, 225, 255 }, 0.32f));
        }
    }

    // ---- minimap (top-right): the whole field at a glance ----
    {
        const int   MM = 170;
        const int   mx = GetScreenWidth() - MM - 14;
        const int   my = 96;
        DrawRectangle(mx - 4, my - 4, MM + 8, MM + 8, Fade(BLACK, 0.55f));
        DrawRectangleLines(mx - 4, my - 4, MM + 8, MM + 8, Fade(RAYWHITE, 0.3f));
        auto toMap = [&](Vector3 p) {
            return Vector2{ mx + (p.x + ARENA) / (2 * ARENA) * MM,
                            my + (ARENA - p.z) / (2 * ARENA) * MM };   // +z is up
        };
        if (B.hasWall) {   // the wall with its gate gap
            const float wy = my + (ARENA - WALL_Z) / (2 * ARENA) * MM;
            const float gl = mx + (-GATE_HALF + ARENA) / (2 * ARENA) * MM;
            const float gr = mx + ( GATE_HALF + ARENA) / (2 * ARENA) * MM;
            DrawLineEx({ (float)mx, wy }, { gl, wy }, 2, GRAY);
            DrawLineEx({ gr, wy }, { (float)(mx + MM), wy }, 2, GRAY);
        }
        for (const Soldier& s : B.soldiers) {
            if (s.hp <= 0) continue;
            const Color dot = s.team == Team::Enemy ? RED
                              : (s.ally ? Color{ 120, 190, 255, 255 } : GREEN);
            DrawCircleV(toMap(s.pos), 2, dot);
        }
        const Vector2 hp2 = toMap(B.pPos);
        DrawCircleV(hp2, 4, Color{ 40, 120, 255, 255 });
        DrawLineEx(hp2, { hp2.x + sinf(B.yaw) * 9, hp2.y - cosf(B.yaw) * 9 }, 2, RAYWHITE);
    }

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
                                       FormationType::Square, FormationType::Spread,
                                       FormationType::ShieldWall };
        for (int i = 0; i < 5; ++i) {
            const bool sel = (B.formation == opts[i]);
            ui::Text(TextFormat("[%d] %s%s", i + 1, FormationName(opts[i]), sel ? "   <" : ""),
                     px + 22, y, 22, sel ? GOLD : RAYWHITE);
            y += 30;
        }
        y += 14;
        ui::Text(TextFormat("Ranks: %d", B.ranks), px + 22, y, 22, RAYWHITE);      y += 28;
        ui::Text("[ and ] : fewer / more ranks", px + 22, y, 16, Fade(RAYWHITE, 0.7f)); y += 34;
        ui::Text("Charge attacks; Line / Square /", px + 22, y, 16, Fade(RAYWHITE, 0.7f)); y += 20;
        ui::Text("Spread hold that shape and fight.", px + 22, y, 16, Fade(RAYWHITE, 0.7f)); y += 20;
        ui::Text("Shield Wall: braced, slow, hard.", px + 22, y, 16, Fade(RAYWHITE, 0.7f)); y += 30;
        ui::Text("~ closes this menu.", px + 22, y, 16, Fade(RAYWHITE, 0.7f));
    }

    // Opening banner: who stands against whom.
    B.introTimer = fmaxf(0.0f, B.introTimer - GetFrameTime());
    if (B.introTimer > 0 && !B.over) {
        const float a = fminf(B.introTimer / 0.6f, 1.0f);   // fade out at the end
        int own = 0, foes = 0;
        for (const Soldier& s : B.soldiers)
            (s.team == Team::Enemy ? foes : own) += (s.hp > 0) ? 1 : 0;
        const char* head = B.setup.siege ? "STORM THE WALLS" : "BATTLE IS JOINED";
        // Name the foe at the horn (V24): a lord, a crown, or a garrison.
        const char* nums = B.setup.enemyName.empty()
            ? TextFormat("%d men against %d", own, foes)
            : TextFormat("%s — %d men against your %d", B.setup.enemyName.c_str(), foes, own);
        const int w1 = ui::MeasureTitle(head, 52);
        const int w2 = ui::Measure(nums, 24);
        const int cy = GetScreenHeight() / 3;
        DrawRectangle(0, cy - 16, GetScreenWidth(), 116, Fade(BLACK, 0.6f * a));
        ui::Title(head, (GetScreenWidth() - w1) / 2, cy, 52, Fade(GOLD, a));
        ui::Text(nums, (GetScreenWidth() - w2) / 2, cy + 62, 24, Fade(RAYWHITE, a));
    }

    // The line breaks: a short red flash of intent.
    B.routBanner = fmaxf(0.0f, B.routBanner - GetFrameTime());
    if (B.routBanner > 0 && B.introTimer <= 0 && !B.over) {
        const float ra = fminf(B.routBanner / 0.5f, 1.0f);
        const int   rw = ui::Measure(B.routText, 40);
        ui::Title(B.routText, (GetScreenWidth() - rw) / 2, 150, 40, Fade(GOLD, ra));
    }
    B.pickupTimer = fmaxf(0.0f, B.pickupTimer - GetFrameTime());
    if (B.pickupTimer > 0 && !B.over) {   // the field re-arms you (V39)
        const float pa = fminf(B.pickupTimer / 0.4f, 1.0f);
        const int pw = ui::Measure(B.pickupMsg.c_str(), 24);
        ui::Text(B.pickupMsg.c_str(), (GetScreenWidth() - pw) / 2,
                 GetScreenHeight() - 150, 24, Fade(GOLD, pa));
    } else if (!B.over && B.pHp > 0) {
        // A quiet prompt when a fallen man's weapon lies in reach.
        for (const Soldier& s : B.soldiers) {
            if (s.hp > 0 || s.escaped || s.looted) continue;
            Vector3 to = Vector3Subtract(s.pos, B.pPos);
            to.y = 0;
            if (Vector3Length(to) > 2.5f) continue;
            const Loadout& lo = TroopLoadout(c, s.troop);
            if (lo.weaponCount() <= 0) continue;
            const char* hint = "[G] take up the fallen weapon";
            const int hw = ui::Measure(hint, 20);
            ui::Text(hint, (GetScreenWidth() - hw) / 2, GetScreenHeight() - 150,
                     20, Fade(RAYWHITE, 0.75f));
            break;
        }
    }
    B.bannerFlash = fmaxf(0.0f, B.bannerFlash - GetFrameTime());
    if (B.bannerFlash > 0 && B.introTimer <= 0 && !B.over) {   // V32
        const float ba = fminf(B.bannerFlash / 0.5f, 1.0f);
        const char* bt = B.bannerFellOurs ? "YOUR BANNER FALLS!"
                                          : "THEIR BANNER FALLS!";
        const int bw = ui::MeasureTitle(bt, 40);
        ui::Title(bt, (GetScreenWidth() - bw) / 2, 205, 40,
                  Fade(B.bannerFellOurs ? RED : GOLD, ba));
    }
    B.cryTimer = fmaxf(0.0f, B.cryTimer - GetFrameTime());
    if (B.cryTimer > 0 && B.introTimer <= 0 && !B.over) {
        const float a = fminf(B.cryTimer / 0.5f, 1.0f);
        const char* head = B.cryText;
        const int w = ui::MeasureTitle(head, 44);
        const int cy = GetScreenHeight() / 3;
        DrawRectangle(0, cy - 12, GetScreenWidth(), 74, Fade(BLACK, 0.4f * a));
        ui::Title(head, (GetScreenWidth() - w) / 2, cy, 44, Fade(Color{ 235, 90, 70, 255 }, a));
    }

    // Knocked senseless: spectate while the warband decides the field.
    if (B.heroDown && !B.over) {
        const char* head = "STRUCK DOWN";
        const char* sub  = "Your warband fights on...";
        const int w1 = ui::MeasureTitle(head, 44);
        const int w2 = ui::Measure(sub, 22);
        const int cy = GetScreenHeight() / 3;
        DrawRectangle(0, cy - 12, GetScreenWidth(), 96, Fade(BLACK, 0.5f));
        ui::Title(head, (GetScreenWidth() - w1) / 2, cy, 44, Fade(RED, 0.9f));
        ui::Text(sub, (GetScreenWidth() - w2) / 2, cy + 54, 22, Fade(RAYWHITE, 0.9f));
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
    v.heroMounted = B.mounted;
    v.heroHorseHp = B.pHorseHp;
    v.order       = OrderName(B.order);
    v.formation   = FormationName(B.formation);
    v.climbPoints = (int)g_climbs.size();
    v.raining     = B.raining;
    v.night       = B.night;
    v.heroKills   = B.heroKills;
    v.enemyName   = B.setup.enemyName;
    v.heroKicks   = B.heroKicksLanded;
    v.bannerOwn   = B.bannerIdx[0] >= 0 && B.soldiers[B.bannerIdx[0]].hp > 0;
    v.bannerEnemy = B.bannerIdx[1] >= 0 && B.soldiers[B.bannerIdx[1]].hp > 0;
    v.looseHorses = (int)B.looseHorses.size();
    {
        const Vector3 a = B.order == OrderType::Hold ? B.holdPos : B.pPos;
        int   own = 0;
        float sum = 0;
        for (const Soldier& s : B.soldiers)
            if (s.team == Team::Player && !s.ally && s.hp > 0 && !s.escaped) {
                own++;
                sum += Vector3Distance(s.pos, a);
            }
        v.ownAvgDistToAnchor = own > 0 ? sum / own : 0;
    }
    v.over         = B.over;
    v.won          = B.won;
    return v;
}
