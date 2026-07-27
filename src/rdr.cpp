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
BoxBuckets g_pills;   // V179: pill/ellipsoid instances, same transform shape
SkinBuckets g_skinBoxes;   // V180: armour-textured instances, colour|skin keyed
SkinBuckets g_skinPills;

unsigned Key(Color c) {
    return (unsigned)c.r | ((unsigned)c.g << 8) | ((unsigned)c.b << 16) |
           ((unsigned)c.a << 24);
}

unsigned long long Key64(Color c, int skin) {
    return (unsigned long long)Key(c) |
           ((unsigned long long)(unsigned)skin << 32);
}

// The oriented a->b frame both box and pill pushes share. `w` is the full
// cross-section scale, `sy` the along-axis scale.
Matrix OrientedFrame(Vector3 a, Vector3 b, float w, float sy) {
    const Vector3 mid = Vector3Scale(Vector3Add(a, b), 0.5f);
    const Vector3 d = Vector3Subtract(b, a);
    const float len = Vector3Length(d);
    const Vector3 y  = Vector3Scale(d, 1.0f / len);
    const Vector3 up = fabsf(y.y) < 0.99f ? Vector3{ 0, 1, 0 } : Vector3{ 1, 0, 0 };
    const Vector3 x  = Vector3Normalize(Vector3CrossProduct(up, y));
    const Vector3 z  = Vector3CrossProduct(x, y);
    return { x.x * w, y.x * sy, z.x * w, mid.x,
             x.y * w, y.y * sy, z.y * w, mid.y,
             x.z * w, y.z * sy, z.z * w, mid.z,
             0.0f,    0.0f,     0.0f,    1.0f };
}
}  // namespace

BoxBuckets& Buckets() { return g_buckets; }
BoxBuckets& PillBuckets() { return g_pills; }

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

// V179: a real rounded primitive. The recorded transform maps the UNIT
// SPHERE (diameter 1) onto a capsule-ish ellipsoid along a->b: cross-section
// diameter 2.2r (reads as massive as the 1.8r-wide box did), length len+2r
// so the rounded caps land where the box's flat ends did.
void PushPill(Vector3 a, Vector3 b, float r, Color c) {
    const Vector3 mid = Vector3Scale(Vector3Add(a, b), 0.5f);
    const Vector3 d = Vector3Subtract(b, a);
    const float len = Vector3Length(d);
    const float w = r * 2.2f;
    if (len < 0.001f) {
        g_pills[Key(c)].push_back(
            MatrixMultiply(MatrixScale(r * 2.0f, r * 2.0f, r * 2.0f),
                           MatrixTranslate(mid.x, mid.y, mid.z)));
        return;
    }
    const Vector3 y  = Vector3Scale(d, 1.0f / len);
    const Vector3 up = fabsf(y.y) < 0.99f ? Vector3{ 0, 1, 0 } : Vector3{ 1, 0, 0 };
    const Vector3 x  = Vector3Normalize(Vector3CrossProduct(up, y));
    const Vector3 z  = Vector3CrossProduct(x, y);
    const float sy = len + r * 2.0f;
    const Matrix m = { x.x * w, y.x * sy, z.x * w, mid.x,
                       x.y * w, y.y * sy, z.y * w, mid.y,
                       x.z * w, y.z * sy, z.z * w, mid.z,
                       0.0f,    0.0f,     0.0f,    1.0f };
    g_pills[Key(c)].push_back(m);
}

// V180: skinned pushes — same transforms, uint64 colour|skin buckets. The
// executors sample the armour atlas row `skin` and modulate the colour.
void PushBoxSkinned(const Matrix& m, Color c, int skin) {
    g_skinBoxes[Key64(c, skin)].push_back(m);
}

void PushOrientedBoxSkinned(Vector3 a, Vector3 b, float r, Color c, int skin) {
    const float len = Vector3Length(Vector3Subtract(b, a));
    if (len < 0.001f) {
        const Vector3 mid = Vector3Scale(Vector3Add(a, b), 0.5f);
        g_skinBoxes[Key64(c, skin)].push_back(
            MatrixMultiply(MatrixScale(r * 2.0f, r * 2.0f, r * 2.0f),
                           MatrixTranslate(mid.x, mid.y, mid.z)));
        return;
    }
    g_skinBoxes[Key64(c, skin)].push_back(OrientedFrame(a, b, r * 1.8f, len + r));
}

void PushPillSkinned(Vector3 a, Vector3 b, float r, Color c, int skin) {
    const float len = Vector3Length(Vector3Subtract(b, a));
    if (len < 0.001f) {
        const Vector3 mid = Vector3Scale(Vector3Add(a, b), 0.5f);
        g_skinPills[Key64(c, skin)].push_back(
            MatrixMultiply(MatrixScale(r * 2.0f, r * 2.0f, r * 2.0f),
                           MatrixTranslate(mid.x, mid.y, mid.z)));
        return;
    }
    g_skinPills[Key64(c, skin)].push_back(
        OrientedFrame(a, b, r * 2.2f, len + r * 2.0f));
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
    // Pills (V179): same material/shader, sphere mesh — mesh-agnostic path.
    if (st.sphere)
        for (auto& [key, mats] : g_pills) {
            if (mats.empty()) continue;
            st.mat->maps[MATERIAL_MAP_DIFFUSE].color =
                Color{ (unsigned char)(key & 0xFF),
                       (unsigned char)((key >> 8) & 0xFF),
                       (unsigned char)((key >> 16) & 0xFF),
                       (unsigned char)((key >> 24) & 0xFF) };
            DrawMeshInstanced(*st.sphere, *st.mat, mats.data(), (int)mats.size());
            mats.clear();
        }
    for (auto& [key, mats] : g_pills) mats.clear();   // sphere-less caller

    // Armour skins (V180): one draw per (colour, skin) bucket, the atlas row
    // selected by the skinRect uniform. The atlas rides the material's
    // SPECULAR slot, bound by raylib automatically; shaders without the
    // uniform (skinRectLoc -1) just drop the skinned buckets to plain draws.
    const bool skinned = st.skinRectLoc >= 0;
    auto drawSkinned = [&](SkinBuckets& src, Mesh* mesh) {
        for (auto& [key, mats] : src) {
            if (mats.empty() || !mesh) { mats.clear(); continue; }
            const unsigned col = (unsigned)(key & 0xFFFFFFFFu);
            const int skin = (int)(key >> 32);
            st.mat->maps[MATERIAL_MAP_DIFFUSE].color =
                Color{ (unsigned char)(col & 0xFF),
                       (unsigned char)((col >> 8) & 0xFF),
                       (unsigned char)((col >> 16) & 0xFF),
                       (unsigned char)((col >> 24) & 0xFF) };
            if (skinned) {
                const float rect[4] = { 0.0f, skin * 0.25f, 1.0f,
                                        (skin + 1) * 0.25f };
                SetShaderValue(st.shader, st.skinRectLoc, rect, SHADER_UNIFORM_VEC4);
            }
            DrawMeshInstanced(*mesh, *st.mat, mats.data(), (int)mats.size());
            mats.clear();
        }
    };
    drawSkinned(g_skinBoxes, st.cube);
    drawSkinned(g_skinPills, st.sphere);
    if (skinned) {   // restore the plain path for the next frame's boxes
        const float none[4] = { 0, 0, 0, 0 };
        SetShaderValue(st.shader, st.skinRectLoc, none, SHADER_UNIFORM_VEC4);
    }
}

namespace {
Texture2D g_vkTex = { 0 };
bool g_vkPending = false;

// The Vulkan road (V163): flatten the buckets into the vkarmy instance
// shape, render them offscreen on the Vulkan device with the live camera,
// and stage the result for PresentVulkan() after EndMode3D().
bool FlushVulkan(const RaylibInstancedState& st) {
    static std::vector<float> inst, pills;
    auto flatten = [](BoxBuckets& src, std::vector<float>& out) {
        out.clear();
        int n = 0;
        for (auto& [key, mats] : src) {
            const float r = (key & 0xFF) / 255.0f, g = ((key >> 8) & 0xFF) / 255.0f;
            const float b = ((key >> 16) & 0xFF) / 255.0f, a = ((key >> 24) & 0xFF) / 255.0f;
            for (const Matrix& m : mats) {
                const float16 f = MatrixToFloatV(m);   // column-major, as GLSL wants
                out.insert(out.end(), f.v, f.v + 16);
                out.push_back(r); out.push_back(g); out.push_back(b); out.push_back(a);
                ++n;
            }
        }
        return n;
    };
    const int count = flatten(g_buckets, inst);
    const int pillCount = flatten(g_pills, pills);   // V179
    // Skinned streams (V180): packed the same way, plus (skin, count) runs.
    static std::vector<float> skinBoxes, skinPills;
    static std::vector<int> sbSkin, sbCount, spSkin, spCount;
    auto flattenSkinned = [](SkinBuckets& src, std::vector<float>& out,
                             std::vector<int>& segSkin, std::vector<int>& segCount) {
        out.clear();
        segSkin.clear();
        segCount.clear();
        for (auto& [key, mats] : src) {
            if (mats.empty()) continue;
            const unsigned col = (unsigned)(key & 0xFFFFFFFFu);
            const float r = (col & 0xFF) / 255.0f, g = ((col >> 8) & 0xFF) / 255.0f;
            const float b = ((col >> 16) & 0xFF) / 255.0f, a = ((col >> 24) & 0xFF) / 255.0f;
            for (const Matrix& m : mats) {
                const float16 f = MatrixToFloatV(m);
                out.insert(out.end(), f.v, f.v + 16);
                out.push_back(r); out.push_back(g); out.push_back(b); out.push_back(a);
            }
            segSkin.push_back((int)(key >> 32));
            segCount.push_back((int)mats.size());
        }
    };
    flattenSkinned(g_skinBoxes, skinBoxes, sbSkin, sbCount);
    flattenSkinned(g_skinPills, skinPills, spSkin, spCount);
    // GL clip z is [-1,1], Vulkan wants [0,1]: append the classic fix-up.
    Matrix fix = MatrixIdentity();
    fix.m10 = 0.5f;
    fix.m14 = 0.5f;
    const Matrix vp = MatrixMultiply(
        MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection()), fix);
    const float16 vpf = MatrixToFloatV(vp);
    const float sun[4] = { st.sun.x, st.sun.y, st.sun.z, 0.0f };
    const int w = GetScreenWidth(), h = GetScreenHeight();
    // Sun shadow matrix (V178): same ortho the GL road uses (gfx.cpp
    // ShadowBegin), centred a little ahead of the camera, z-fixed for
    // Vulkan. The camera comes back out of the modelview we already have.
    const Matrix invView = MatrixInvert(rlGetMatrixModelview());
    const Vector3 camPos = { invView.m12, invView.m13, invView.m14 };
    const Vector3 fwd = Vector3Normalize({ -invView.m8, -invView.m9, -invView.m10 });
    const Vector3 center = Vector3Add(camPos, Vector3Scale(fwd, 60.0f));
    const Vector3 eye = Vector3Add(center, Vector3Scale(st.sun, -160.0f));
    const float S = 110.0f;
    const Matrix lightVP = MatrixMultiply(
        MatrixMultiply(MatrixLookAt(eye, center, { 0, 1, 0 }),
                       MatrixOrtho(-S, S, -S, S, 5.0, 400.0)), fix);
    const float16 lvpf = MatrixToFloatV(lightVP);
    const int flags = GetSettings().shadows ? 1 : 0;
    const unsigned char* px =
        VulkanRenderFrame(vpf.v, sun, lvpf.v, flags, inst.data(), count,
                          pills.data(), pillCount,
                          skinBoxes.data(), sbSkin.data(), sbCount.data(),
                          (int)sbSkin.size(),
                          skinPills.data(), spSkin.data(), spCount.data(),
                          (int)spSkin.size(), w, h);
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
    for (auto& [key, mats] : g_pills) mats.clear();
    for (auto& [key, mats] : g_skinBoxes) mats.clear();
    for (auto& [key, mats] : g_skinPills) mats.clear();
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
// Segment stream (V177): runs of verts sharing one pipeline binding.
// tex -1 = atlas/solid (text pipeline); >=0 = registered RGBA texture.
std::vector<int> g_segTex, g_segCount;
std::unordered_map<unsigned, int> g_texIds;   // raylib texture id -> vk id

void SegAppend(int tex, int n) {
    if (!g_segTex.empty() && g_segTex.back() == tex)
        g_segCount.back() += n;
    else {
        g_segTex.push_back(tex);
        g_segCount.push_back(n);
    }
}
}  // namespace

bool VulkanUiActive() {
    return g_uiRecord && GetSettings().renderer == 1 && VulkanExecutorReady();
}

void SetUiRecording(bool on) { g_uiRecord = on; }

namespace {
Camera2D g_uiCam{};
bool g_uiCamOn = false;
}  // namespace

void SetUiCamera(const Camera2D* cam) {
    g_uiCamOn = cam != nullptr;
    if (cam) g_uiCam = *cam;
}

void PushUiVerts(const UiVert* v, int n) {
    const size_t base = g_uiVerts.size();
    g_uiVerts.insert(g_uiVerts.end(), v, v + n);
    if (g_uiCamOn)
        for (size_t i = base; i < g_uiVerts.size(); ++i) {
            const Vector2 s = GetWorldToScreen2D(
                { g_uiVerts[i].x, g_uiVerts[i].y }, g_uiCam);
            g_uiVerts[i].x = s.x;
            g_uiVerts[i].y = s.y;
        }
    SegAppend(-1, n);
}

bool PushUiTexQuad(const Texture& tex, Rectangle src, Rectangle dst, Color tint,
                   bool dynamic) {
    if (!VulkanUiActive() || tex.id == 0) return false;
    int vkId;
    auto it = g_texIds.find(tex.id);
    if (it != g_texIds.end()) {
        vkId = it->second;
        if (dynamic && vkId >= 0) {   // live preview: refresh the pixels
            Image im = LoadImageFromTexture(tex);
            ImageFormat(&im, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            VulkanUpdateUiTexture(vkId, (const unsigned char*)im.data,
                                  im.width, im.height);
            UnloadImage(im);
        }
    } else {
        Image im = LoadImageFromTexture(tex);
        ImageFormat(&im, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        vkId = VulkanRegisterUiTexture((const unsigned char*)im.data,
                                       im.width, im.height);
        UnloadImage(im);
        g_texIds[tex.id] = vkId;   // cache even on failure: don't retry hot
    }
    if (vkId < 0) return false;
    const float u0 = src.x / tex.width, v0 = src.y / tex.height;
    const float u1 = (src.x + src.width) / tex.width;
    const float v1 = (src.y + src.height) / tex.height;
    const float cr = tint.r / 255.0f, cg = tint.g / 255.0f;
    const float cb = tint.b / 255.0f, ca = tint.a / 255.0f;
    const float x0 = dst.x, y0 = dst.y, x1 = dst.x + dst.width, y1 = dst.y + dst.height;
    UiVert q[6] = {
        { x0, y0, u0, v0, cr, cg, cb, ca }, { x1, y0, u1, v0, cr, cg, cb, ca },
        { x1, y1, u1, v1, cr, cg, cb, ca }, { x0, y0, u0, v0, cr, cg, cb, ca },
        { x1, y1, u1, v1, cr, cg, cb, ca }, { x0, y1, u0, v1, cr, cg, cb, ca },
    };
    if (g_uiCamOn)
        for (UiVert& u : q) {
            const Vector2 s = GetWorldToScreen2D({ u.x, u.y }, g_uiCam);
            u.x = s.x;
            u.y = s.y;
        }
    g_uiVerts.insert(g_uiVerts.end(), q, q + 6);
    SegAppend(vkId, 6);
    return true;
}

void PresentVulkanUi() {
    if (g_uiVerts.empty()) return;
    const int w = GetScreenWidth(), h = GetScreenHeight();
    const unsigned char* px =
        VulkanRenderUi(g_uiVerts.data(), (int)g_uiVerts.size(),
                       g_segTex.data(), g_segCount.data(),
                       (int)g_segTex.size(), w, h);
    g_uiVerts.clear();
    g_segTex.clear();
    g_segCount.clear();
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
