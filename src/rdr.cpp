#include "rdr.h"
#include "settings.h"
#include "raymath.h"
#include "rlgl.h"
#include <cstdlib>
#include <cstring>
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

// V198: the flattened streams live at file scope so a NATIVE frame can hold
// them from the 3D flush until the end-of-frame present (sky + HUD verts
// only exist after the HUD has drawn).
std::vector<float> g_fInst, g_fPills, g_fCyls, g_fSkinBoxes, g_fSkinPills,
    g_fSkinCyls;
std::vector<int> g_fSbSkin, g_fSbCount, g_fSpSkin, g_fSpCount, g_fScSkin,
    g_fScCount;
int g_fCount = 0, g_fPillCount = 0, g_fCylCount = 0;
float g_fVp[16], g_fLightVp[16], g_fSun[4];
int g_fFlags = 0;
bool g_natPending = false;    // a native frame is being built this frame
bool g_natEnabled = true;     // capture modes force the bridge

// V200: `wantNative` is an EXPLICIT opt-in from the scene (battle passes it
// through FlushSubmit). It used to be inferred here - which meant the TOWN's
// flush also stashed its recording for a native present that only battle
// knows how to finish: black scene, eaten buckets. Scenes are GL/bridge by
// default; a scene goes native only when it says so and routes its own
// sky + HUD like battle does.
bool FlushVulkanSubmit(const RaylibInstancedState& st, bool wantNative) {
    std::vector<float>& inst = g_fInst;
    std::vector<float>& pills = g_fPills;
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
    std::vector<float>& cyls = g_fCyls;
    const int cylCount = flatten(g_cyls, cyls);      // V185: capsule shafts
    // Skinned streams (V180): packed the same way, plus (skin, count) runs.
    std::vector<float>& skinBoxes = g_fSkinBoxes;
    std::vector<float>& skinPills = g_fSkinPills;
    std::vector<int>& sbSkin = g_fSbSkin;
    std::vector<int>& sbCount = g_fSbCount;
    std::vector<int>& spSkin = g_fSpSkin;
    std::vector<int>& spCount = g_fSpCount;
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
    std::vector<float>& skinCyls = g_fSkinCyls;
    std::vector<int>& scSkin = g_fScSkin;
    std::vector<int>& scCount = g_fScCount;
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
    // V198: NATIVE mode holds the streams until the end-of-frame present
    // (the HUD has not been drawn yet); the bridge submits right here.
    g_fCount = count;
    g_fPillCount = pillCount;
    g_fCylCount = cylCount;
    memcpy(g_fVp, vpf.v, sizeof(g_fVp));
    memcpy(g_fLightVp, lvpf.v, sizeof(g_fLightVp));
    memcpy(g_fSun, sun, sizeof(g_fSun));
    g_fFlags = flags;
    const bool natThisFrame =
        wantNative && g_natEnabled && !g_natPending && VulkanNativeAvailable();
    if (!natThisFrame &&
        !VulkanSubmitFrame(vpf.v, sun, lvpf.v, flags, inst.data(), count,
                           pills.data(), pillCount,
                           skinBoxes.data(), sbSkin.data(), sbCount.data(),
                           (int)sbSkin.size(),
                           skinPills.data(), spSkin.data(), spCount.data(),
                           (int)spSkin.size(),
                           cyls.data(), cylCount,
                           skinCyls.data(), scSkin.data(), scCount.data(),
                           (int)scSkin.size(), w, h,
                           NULL, 0, NULL, 0, NULL, NULL, 0, 0))
        return false;
    // The streams own copies of everything now - the recording is spent.
    for (auto& [key, mats] : g_buckets) mats.clear();
    for (auto& [key, mats] : g_pills) mats.clear();
    for (auto& [key, mats] : g_skinBoxes) mats.clear();
    for (auto& [key, mats] : g_skinPills) mats.clear();
    for (auto& [key, mats] : g_cyls) mats.clear();
    for (auto& [key, mats] : g_skinCyls) mats.clear();
    if (natThisFrame) {
        g_natPending = true;   // presented by PresentVulkanUi at frame end
        g_vkSubmitW = w;
        g_vkSubmitH = h;
        return true;
    }
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
    return FlushVulkanSubmit(st, false) && FlushVulkanResolve();
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
std::vector<UiVert> g_bgVerts;   // V198: the sky underlay (drawn UNDER 3D)
bool g_uiUnderlay = false;
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
    // V198: it came back - through the NATIVE swapchain (RENDERER.md tail).
    // ui:: primitives record whenever this frame is bound for the native
    // present: the sky underlay bracket, or the HUD once the 3D flush has
    // marked the frame native. Everywhere else (menus, campaign, bridge
    // mode) the V196 parking holds and ui:: draws straight GL.
    return g_uiRecord && (g_uiUnderlay || g_natPending);
}

// V198: while ON, ui:: primitives record into the underlay list the native
// frame draws BEFORE the 3D scene - the battle sky lives there.
void SetUiUnderlay(bool on) { g_uiUnderlay = on; }

// V198: capture modes (--shots) force the readback bridge so screenshots
// keep working; the native road resumes when re-enabled.
void SetNativeEnabled(bool on) { g_natEnabled = on; }

bool NativeActive() {
    return GetSettings().renderer == 1 && g_natEnabled &&
           VulkanExecutorReady() && VulkanNativeAvailable();
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
    if (g_uiUnderlay) {   // V198: the sky records under the 3D scene
        g_bgVerts.insert(g_bgVerts.end(), v, v + n);
        return;
    }
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

// V200: the FRAME CONTRACT, checked at the end-of-frame chokepoint every
// screen already passes through. A healthy frame ends with: no recorded
// buckets waiting (every scene flushed what it pushed), the underlay closed,
// and no native frame left dangling. Violations are the bugs that used to
// surface as "the town is black" two screens later - now they log loudly
// and the state is reset so the NEXT frame starts clean.
namespace {
void ValidateFrameContract(const char* where) {
    size_t leaked = 0;
    for (auto& [k, m] : g_buckets) leaked += m.size();
    for (auto& [k, m] : g_pills) leaked += m.size();
    for (auto& [k, m] : g_cyls) leaked += m.size();
    for (auto& [k, m] : g_skinBoxes) leaked += m.size();
    for (auto& [k, m] : g_skinPills) leaked += m.size();
    for (auto& [k, m] : g_skinCyls) leaked += m.size();
    static double lastCry = -10.0;
    const double now = GetTime();
    if (leaked > 0) {
        if (now - lastCry > 1.0) {
            TraceLog(LOG_WARNING,
                     "rdr: FRAME CONTRACT: %d recorded instances never "
                     "flushed (%s) - dropped; the scene is missing a Flush",
                     (int)leaked, where);
            lastCry = now;
        }
        for (auto& [k, m] : g_buckets) m.clear();
        for (auto& [k, m] : g_pills) m.clear();
        for (auto& [k, m] : g_cyls) m.clear();
        for (auto& [k, m] : g_skinBoxes) m.clear();
        for (auto& [k, m] : g_skinPills) m.clear();
        for (auto& [k, m] : g_skinCyls) m.clear();
    }
    if (g_uiUnderlay) {
        if (now - lastCry > 1.0) {
            TraceLog(LOG_WARNING,
                     "rdr: FRAME CONTRACT: underlay left ON (%s) - reset",
                     where);
            lastCry = now;
        }
        g_uiUnderlay = false;
    }
}
}  // namespace

void PresentVulkanUi() {
    // V198: the native frame presents HERE - everything is recorded by now
    // (sky underlay, army streams from the 3D flush, HUD overlay).
    if (g_natPending) {
        g_natPending = false;
        VulkanSubmitFrame(
            g_fVp, g_fSun, g_fLightVp, g_fFlags, g_fInst.data(), g_fCount,
            g_fPills.data(), g_fPillCount, g_fSkinBoxes.data(),
            g_fSbSkin.data(), g_fSbCount.data(), (int)g_fSbSkin.size(),
            g_fSkinPills.data(), g_fSpSkin.data(), g_fSpCount.data(),
            (int)g_fSpSkin.size(), g_fCyls.data(), g_fCylCount,
            g_fSkinCyls.data(), g_fScSkin.data(), g_fScCount.data(),
            (int)g_fScSkin.size(), g_vkSubmitW, g_vkSubmitH,
            g_bgVerts.data(), (int)g_bgVerts.size(), g_uiVerts.data(),
            (int)g_uiVerts.size(), g_segTex.data(), g_segCount.data(),
            (int)g_segTex.size(), 1);
        // Debug tap (V198): OWB_NATIVE_DUMP=<dir> exports every 60th
        // presented frame straight off the GPU - the only honest screenshot
        // of the native road (the GL buffer behind the overlay is blank).
        static const char* dumpDir = getenv("OWB_NATIVE_DUMP");
        static int dumpN = 0;
        if (dumpDir) {
            VulkanNativeDebugDump(true);
            if (++dumpN % 60 == 0 && dumpN <= 600) {
                const unsigned char* px = VulkanResolveFrame();
                if (px) {
                    Image im = { (void*)px, g_vkSubmitW, g_vkSubmitH, 1,
                                 PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
                    ExportImage(im, TextFormat("%s/nat_%04d.png", dumpDir, dumpN));
                }
            }
        }
        // Failure = one dropped frame; NativeActive() reads false next frame
        // and the whole scene renders through the bridge again.
        g_bgVerts.clear();
        g_uiVerts.clear();
        g_segTex.clear();
        g_segCount.clear();
        ValidateFrameContract("native present");
        return;
    }
    VulkanNativeHide();   // any non-native frame pulls the overlay down
    g_bgVerts.clear();    // stragglers from an aborted native frame
    ValidateFrameContract("frame end");
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
           FlushVulkanSubmit(st, true);   // battle's explicit native opt-in
}

void Flush(const RaylibInstancedState& st) {
    if (g_natPending) {   // V198: the native frame holds its streams until
        FlushRaylib(st);  // the end-of-frame present - do NOT re-flatten
        return;           // (that zeroed the army); just drain stragglers.
    }
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
