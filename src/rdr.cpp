#include "rdr.h"
#include "settings.h"
#include "raymath.h"

// The seam's recording + raylib executor (V160). The recording types are
// shared verbatim with the Vulkan backend (tools/vkarmy.c consumes the same
// mat4+colour instance shape); this file is the only place the GL path
// touches them.

namespace rdr {

namespace {
BoxBuckets g_buckets;

unsigned Key(Color c) {
    return (unsigned)c.r | ((unsigned)c.g << 8) | ((unsigned)c.b << 16) |
           ((unsigned)c.a << 24);
}
}  // namespace

BoxBuckets& Buckets() { return g_buckets; }

void PushBox(const Matrix& m, Color c) { g_buckets[Key(c)].push_back(m); }

void PushOrientedBox(Vector3 a, Vector3 b, float r, Color c) {
    const Vector3 mid = Vector3Scale(Vector3Add(a, b), 0.5f);
    Vector3 d = Vector3Subtract(b, a);
    const float len = Vector3Length(d);
    const float w = r * 1.8f;
    if (len < 0.001f) {
        PushBox(MatrixMultiply(MatrixScale(r * 2.0f, r * 2.0f, r * 2.0f),
                               MatrixTranslate(mid.x, mid.y, mid.z)), c);
        return;
    }
    const Vector3 y  = Vector3Scale(d, 1.0f / len);
    const Vector3 up = fabsf(y.y) < 0.99f ? Vector3{ 0, 1, 0 } : Vector3{ 1, 0, 0 };
    const Vector3 x  = Vector3Normalize(Vector3CrossProduct(up, y));
    const Vector3 z  = Vector3CrossProduct(x, y);
    const float sy = len + r;
    const Matrix m = { x.x * w, y.x * sy, z.x * w, mid.x,
                       x.y * w, y.y * sy, z.y * w, mid.y,
                       x.z * w, y.z * sy, z.z * w, mid.z,
                       0.0f,    0.0f,     0.0f,    1.0f };
    PushBox(m, c);
}

void FlushRaylib(const RaylibInstancedState& st) {
    if (st.sunLoc >= 0)
        SetShaderValue(st.shader, st.sunLoc, &st.sun, SHADER_UNIFORM_VEC3);
    for (auto& [key, mats] : g_buckets) {
        if (mats.empty()) continue;
        st.mat->maps[MATERIAL_MAP_DIFFUSE].color =
            Color{ (unsigned char)(key & 0xFF),
                   (unsigned char)((key >> 8) & 0xFF),
                   (unsigned char)((key >> 16) & 0xFF),
                   (unsigned char)((key >> 24) & 0xFF) };
        DrawMeshInstanced(*st.cube, *st.mat, mats.data(), (int)mats.size());
        mats.clear();
    }
}

void Flush(const RaylibInstancedState& st) {
    static bool noted = false;
    if (GetSettings().renderer == 1 && !noted) {
        noted = true;
        TraceLog(LOG_INFO,
                 "rdr: renderer=vulkan requested; executor in progress "
                 "(RENDERER.md) - executing via GL until parity");
    }
    FlushRaylib(st);   // both roads run through the same recording
}

}  // namespace rdr
