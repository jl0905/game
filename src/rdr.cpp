#include "rdr.h"
#include "settings.h"
#include "raymath.h"
#include "rlgl.h"
#include <vector>

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

namespace {
Texture2D g_vkTex = { 0 };
bool g_vkPending = false;

// The Vulkan road (V163): flatten the buckets into the vkarmy instance
// shape, render them offscreen on the Vulkan device with the live camera,
// and stage the result for PresentVulkan() after EndMode3D().
bool FlushVulkan(const RaylibInstancedState& st) {
    static std::vector<float> inst;
    inst.clear();
    int count = 0;
    for (auto& [key, mats] : g_buckets) {
        const float r = (key & 0xFF) / 255.0f, g = ((key >> 8) & 0xFF) / 255.0f;
        const float b = ((key >> 16) & 0xFF) / 255.0f, a = ((key >> 24) & 0xFF) / 255.0f;
        for (const Matrix& m : mats) {
            const float16 f = MatrixToFloatV(m);   // column-major, as GLSL wants
            inst.insert(inst.end(), f.v, f.v + 16);
            inst.push_back(r); inst.push_back(g); inst.push_back(b); inst.push_back(a);
            ++count;
        }
    }
    // GL clip z is [-1,1], Vulkan wants [0,1]: append the classic fix-up.
    Matrix fix = MatrixIdentity();
    fix.m10 = 0.5f;
    fix.m14 = 0.5f;
    const Matrix vp = MatrixMultiply(
        MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection()), fix);
    const float16 vpf = MatrixToFloatV(vp);
    const float sun[4] = { st.sun.x, st.sun.y, st.sun.z, 0.0f };
    const int w = GetScreenWidth(), h = GetScreenHeight();
    const unsigned char* px =
        VulkanRenderFrame(vpf.v, sun, inst.data(), count, w, h);
    if (!px) return false;
    if (g_vkTex.id == 0 || g_vkTex.width != w || g_vkTex.height != h) {
        if (g_vkTex.id) UnloadTexture(g_vkTex);
        Image im = { (void*)px, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        g_vkTex = LoadTextureFromImage(im);
    } else {
        UpdateTexture(g_vkTex, px);
    }
    g_vkPending = true;
    for (auto& [key, mats] : g_buckets) mats.clear();
    return true;
}
}  // namespace

void PresentVulkan() {
    if (!g_vkPending) return;
    g_vkPending = false;
    DrawTexture(g_vkTex, 0, 0, WHITE);   // alpha over the GL scene
}

// ---- HUD/text recording (V173) --------------------------------------------
namespace {
std::vector<UiVert> g_uiVerts;
bool g_uiRecord = true;
Texture2D g_uiTex = { 0 };
}  // namespace

bool VulkanUiActive() {
    return g_uiRecord && GetSettings().renderer == 1 && VulkanExecutorReady();
}

void SetUiRecording(bool on) { g_uiRecord = on; }

void PushUiVerts(const UiVert* v, int n) {
    g_uiVerts.insert(g_uiVerts.end(), v, v + n);
}

void PresentVulkanUi() {
    if (g_uiVerts.empty()) return;
    const int w = GetScreenWidth(), h = GetScreenHeight();
    const unsigned char* px =
        VulkanRenderUi(g_uiVerts.data(), (int)g_uiVerts.size(), w, h);
    g_uiVerts.clear();
    if (!px) return;
    if (g_uiTex.id == 0 || g_uiTex.width != w || g_uiTex.height != h) {
        if (g_uiTex.id) UnloadTexture(g_uiTex);
        Image im = { (void*)px, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        g_uiTex = LoadTextureFromImage(im);
    } else {
        UpdateTexture(g_uiTex, px);
    }
    DrawTexture(g_uiTex, 0, 0, WHITE);
}

void Flush(const RaylibInstancedState& st) {
    if (GetSettings().renderer == 1 && VulkanExecutorReady() && FlushVulkan(st))
        return;        // Vulkan rendered the recording this frame
    FlushRaylib(st);   // the GL road, unchanged
}

}  // namespace rdr
