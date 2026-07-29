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
BoxBuckets g_pills;   // V179: sphere instances (pill CAPS since V185)
BoxBuckets g_cyls;    // V185: cylinder shaft instances - the pill's barrel
SkinBuckets g_skinBoxes;   // V180: armour-textured instances, colour|skin keyed
SkinBuckets g_skinPills;
SkinBuckets g_skinCyls;    // V185: armour-textured shafts

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

namespace {
// A uniform sphere of radius r at p - the pill's end cap.
Matrix CapSphere(Vector3 p, float r) {
    return MatrixMultiply(MatrixScale(r * 2.0f, r * 2.0f, r * 2.0f),
                          MatrixTranslate(p.x, p.y, p.z));
}
// V191: shaft-end caps are drawn a hair OVER-size. At exactly r the
// cylinder's rim circle lies tangentially ON the sphere surface, and the
// two meshes (different tessellations) z-fight in a shimmering ring at
// every pill's shoulder seam. At 1.03r the sphere cleanly overlaps the
// shaft near the seam — no coplanar polygons, no ring.
constexpr float CAP_OVERLAP = 1.03f;
}  // namespace

// V185: a TRUE capsule (user call: "pills are cylindrical except the top
// and bottom which are like balls"). Decomposed into three instances: a
// unit-cylinder shaft scaled (2r, len, 2r) along a->b plus two UNIFORM
// sphere caps at the exact endpoints - the caps stay perfectly round at
// any body length, unlike the old stretched-ellipsoid single instance.
void PushPill(Vector3 a, Vector3 b, float r, Color c) {
    const float len = Vector3Length(Vector3Subtract(b, a));
    if (len < 0.001f) {
        g_pills[Key(c)].push_back(
            CapSphere(Vector3Scale(Vector3Add(a, b), 0.5f), r));
        return;
    }
    g_cyls[Key(c)].push_back(OrientedFrame(a, b, r * 2.0f, len));
    g_pills[Key(c)].push_back(CapSphere(a, r * CAP_OVERLAP));
    g_pills[Key(c)].push_back(CapSphere(b, r * CAP_OVERLAP));
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
        g_skinPills[Key64(c, skin)].push_back(
            CapSphere(Vector3Scale(Vector3Add(a, b), 0.5f), r));
        return;
    }
    // True capsule (V185): skinned shaft + skinned round caps.
    g_skinCyls[Key64(c, skin)].push_back(OrientedFrame(a, b, r * 2.0f, len));
    g_skinPills[Key64(c, skin)].push_back(CapSphere(a, r * CAP_OVERLAP));
    g_skinPills[Key64(c, skin)].push_back(CapSphere(b, r * CAP_OVERLAP));
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
    // Capsule shafts (V185): same shader, cylinder mesh.
    if (st.cyl)
        for (auto& [key, mats] : g_cyls) {
            if (mats.empty()) continue;
            st.mat->maps[MATERIAL_MAP_DIFFUSE].color =
                Color{ (unsigned char)(key & 0xFF),
                       (unsigned char)((key >> 8) & 0xFF),
                       (unsigned char)((key >> 16) & 0xFF),
                       (unsigned char)((key >> 24) & 0xFF) };
            DrawMeshInstanced(*st.cyl, *st.mat, mats.data(), (int)mats.size());
            mats.clear();
        }
    for (auto& [key, mats] : g_cyls) mats.clear();    // cyl-less caller

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
    drawSkinned(g_skinCyls, st.cyl);   // V185: skinned capsule shafts
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
// V197: split into SUBMIT (flatten + queue the GPU work, buckets consumed)
// and RESOLVE (fence-wait + stage the layer) so the caller can overlap the
// GPU render with its own GL drawing between the two.
bool g_vkSubmitPending = false;
int  g_vkSubmitW = 0, g_vkSubmitH = 0;

bool FlushVulkanSubmit(const RaylibInstancedState& st) {
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
    const int pillCount = flatten(g_pills, pills);   // V179 (caps since V185)
    static std::vector<float> cyls;
    const int cylCount = flatten(g_cyls, cyls);      // V185: capsule shafts
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
    static std::vector<float> skinCyls;
    static std::vector<int> scSkin, scCount;
    flattenSkinned(g_skinCyls, skinCyls, scSkin, scCount);   // V185
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
    if (!VulkanSubmitFrame(vpf.v, sun, lvpf.v, flags, inst.data(), count,
                           pills.data(), pillCount,
                           skinBoxes.data(), sbSkin.data(), sbCount.data(),
                           (int)sbSkin.size(),
                           skinPills.data(), spSkin.data(), spCount.data(),
                           (int)spSkin.size(),
                           cyls.data(), cylCount,
                           skinCyls.data(), scSkin.data(), scCount.data(),
                           (int)scSkin.size(), w, h))
        return false;
    // The GPU owns copies of everything now - the recording is spent.
    for (auto& [key, mats] : g_buckets) mats.clear();
    for (auto& [key, mats] : g_pills) mats.clear();
    for (auto& [key, mats] : g_skinBoxes) mats.clear();
    for (auto& [key, mats] : g_skinPills) mats.clear();
    for (auto& [key, mats] : g_cyls) mats.clear();
    for (auto& [key, mats] : g_skinCyls) mats.clear();
    g_vkSubmitPending = true;
    g_vkSubmitW = w;
    g_vkSubmitH = h;
    return true;
}

bool FlushVulkanResolve() {
    if (!g_vkSubmitPending) return false;
    g_vkSubmitPending = false;
    const unsigned char* px = VulkanResolveFrame();
    if (!px) return false;
    const int w = g_vkSubmitW, h = g_vkSubmitH;
    if (g_vkTex.id == 0 || g_vkTex.width != w || g_vkTex.height != h) {
        if (g_vkTex.id) UnloadTexture(g_vkTex);
        Image im = { (void*)px, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        g_vkTex = LoadTextureFromImage(im);
    } else {
        UpdateTexture(g_vkTex, px);
    }
    g_vkPending = true;
    return true;
}

bool FlushVulkan(const RaylibInstancedState& st) {
    return FlushVulkanSubmit(st) && FlushVulkanResolve();
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
    // V196: the Vulkan HUD overlay is PARKED. Its render is synchronous — a
    // submit + fence wait + full-frame CPU readback + GL re-upload in the
    // middle of every frame — which cost more in stalls than it will ever
    // save before the native-swapchain present lands (RENDERER.md). The HUD
    // draws through GL on both backends; the text pipeline stays built and
    // this gate is where it comes back.
    return false;
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

// V197: submit-only entry for callers that overlap GL work before Flush().
// True = the recording was consumed and queued on the Vulkan device; false =
// GL mode (or executor down) - the caller just proceeds to Flush() as ever.
bool FlushSubmit(const RaylibInstancedState& st) {
    return GetSettings().renderer == 1 && VulkanExecutorReady() &&
           FlushVulkanSubmit(st);
}

void Flush(const RaylibInstancedState& st) {
    if (g_vkSubmitPending) {       // V197: second half of a split flush
        FlushVulkanResolve();
        FlushRaylib(st);           // drain any post-submit stragglers via GL
        return;
    }
    if (GetSettings().renderer == 1 && VulkanExecutorReady() && FlushVulkan(st))
        return;        // Vulkan rendered the recording this frame
    FlushRaylib(st);   // the GL road, unchanged
}

}  // namespace rdr
