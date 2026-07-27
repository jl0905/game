#include "ui.h"
#include "rdr.h"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Font loading + text drawing. See ui.h for the contract and assets/fonts.cfg
// for the mod-facing configuration.
// ---------------------------------------------------------------------------
namespace ui {
namespace {

// Loaded fonts. Default-initialised to raylib's built-in font so drawing is
// safe even before LoadFonts() runs or if a configured font fails to load.
Font gBody{};
Font gTitle{};

// Inter-glyph spacing as a fraction of the font size. Kept proportional so text
// looks consistent at every size. Body and title share the same ratio.
constexpr float SPACING_RATIO = 0.06f;

// Highest resolution the glyph atlas is baked at (overridable via fonts.cfg).
// Text drawn smaller is downscaled crisply; larger is softened.
constexpr int DEFAULT_ATLAS = 96;

float Spacing(int fontSize) { return fontSize * SPACING_RATIO; }

std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Locate the assets/ directory. Prefer next to the executable (CMake copies the
// tree there post-build) so the game runs from any working directory; fall back
// to the current directory and its parent for run-from-source cases.
std::string AssetsDir() {
    const std::string appDir = GetApplicationDirectory();  // has trailing slash
    const std::string candidates[] = { appDir + "assets/", "assets/", "../assets/" };
    for (const std::string& c : candidates)
        if (DirectoryExists(c.c_str())) return c;
    return "assets/";
}

// Read a "key = value" line for `key` from the config text, or "" if absent.
std::string ConfigValue(const char* cfg, const std::string& key) {
    if (!cfg) return "";
    std::string text = cfg;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t eol = text.find('\n', pos);
        std::string line = text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos = (eol == std::string::npos) ? text.size() : eol + 1;

        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (Trim(line.substr(0, eq)) == key) return Trim(line.substr(eq + 1));
    }
    return "";
}

// Load one font from an assets-relative path at the atlas size. Returns the
// built-in font on failure so callers always get something drawable.
Font LoadRole(const std::string& assetsDir, const std::string& relPath, int atlas) {
    if (relPath.empty()) return GetFontDefault();
    const std::string full = assetsDir + relPath;
    if (!FileExists(full.c_str())) {
        TraceLog(LOG_WARNING, "UI: font '%s' not found; using default font", full.c_str());
        return GetFontDefault();
    }
    // codepoints = nullptr, count = 0 -> raylib bakes the default ASCII set.
    Font f = LoadFontEx(full.c_str(), atlas, nullptr, 0);
    if (f.texture.id == 0) {
        TraceLog(LOG_WARNING, "UI: failed to load font '%s'; using default font", full.c_str());
        return GetFontDefault();
    }
    // Smooth minification/magnification instead of the blocky nearest default.
    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    return f;
}

bool IsBuiltin(const Font& f) { return f.texture.id == GetFontDefault().texture.id; }

}  // namespace

void LoadFonts() {
    const std::string dir = AssetsDir();
    char* cfg = LoadFileText((dir + "fonts.cfg").c_str());  // null if missing

    int atlas = DEFAULT_ATLAS;
    const std::string atlasStr = ConfigValue(cfg, "atlas");
    if (!atlasStr.empty()) {
        const int v = std::atoi(atlasStr.c_str());
        if (v >= 8 && v <= 512) atlas = v;
    }

    gBody  = LoadRole(dir, ConfigValue(cfg, "body"),  atlas);
    gTitle = LoadRole(dir, ConfigValue(cfg, "title"), atlas);

    // Global text scale (U13): moddable, clamped sane.
    const std::string sc = ConfigValue(cfg, "scale");
    if (!sc.empty()) SetTextScale((float)std::atof(sc.c_str()));

    if (cfg) UnloadFileText(cfg);
}

void UnloadFonts() {
    if (!IsBuiltin(gBody))  UnloadFont(gBody);
    if (!IsBuiltin(gTitle)) UnloadFont(gTitle);
    gBody  = Font{};
    gTitle = Font{};
}

const Font& BodyFont()  { return gBody.texture.id ? gBody  : gBody = GetFontDefault(); }
const Font& TitleFont() { return gTitle.texture.id ? gTitle : gTitle = GetFontDefault(); }

// Global text scale (U13, playtest: "bigger on all screens"). Every draw
// AND every measure route through it, so centred layouts and hover bands
// stay coherent as the scale moves. Moddable via fonts.cfg `scale = 1.2`.
float gScale = 1.2f;

void SetTextScale(float s) {
    gScale = s < 0.8f ? 0.8f : s > 1.6f ? 1.6f : s;
}

// A floor under the small print (V3): HUD-range labels (requested size
// >= 14) never render below 19 px. Sizes under 14 are deliberate fine
// print — mostly world-space map text whose size already encodes the
// zoom — and pass through untouched.
static float Sz(int fontSize) {
    const float s = fontSize * gScale;
    return (fontSize >= 14 && s < 19.0f) ? 19.0f : s;
}

// ---- Vulkan text road (V173) ----------------------------------------------
// When the Vulkan backend is live, glyphs are recorded as screen-space quads
// (rdr::PushUiVerts) and rendered by the Vulkan text pipeline instead of GL.
// The two font atlases are stacked into one R8 texture, uploaded once.
static int gAtlasW = 0, gAtlasH = 0, gTitleYOff = 0;
static bool gAtlasSent = false, gAtlasTried = false;

static bool EnsureVkAtlas() {
    if (gAtlasSent) return true;
    if (gAtlasTried) return false;
    gAtlasTried = true;
    Image ib = LoadImageFromTexture(BodyFont().texture);
    ImageFormat(&ib, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    const bool same = TitleFont().texture.id == BodyFont().texture.id;
    Image it{};
    if (!same) {
        it = LoadImageFromTexture(TitleFont().texture);
        ImageFormat(&it, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    }
    int W = ib.width;
    if (!same && it.width > W) W = it.width;
    const int H = ib.height + (same ? 0 : it.height);
    std::vector<unsigned char> r8((size_t)W * H, 0);
    auto blit = [&](const Image& im, int yo) {
        const unsigned char* p = (const unsigned char*)im.data;
        for (int y = 0; y < im.height; ++y)
            for (int x = 0; x < im.width; ++x)
                r8[(size_t)(y + yo) * W + x] = p[((size_t)y * im.width + x) * 4 + 3];
    };
    blit(ib, 0);
    if (!same) blit(it, ib.height);
    gTitleYOff = same ? 0 : ib.height;
    gAtlasW = W;
    gAtlasH = H;
    rdr::VulkanSetUiAtlas(r8.data(), W, H);
    UnloadImage(ib);
    if (!same) UnloadImage(it);
    gAtlasSent = true;
    return true;
}

// Mirror DrawTextEx's layout, but emit quads at the seam.
static void RecordText(const Font& f, bool title, const char* text, float x,
                       float y, float size, float spacing, Color color) {
    const float scale = size / (float)f.baseSize;
    const float cr = color.r / 255.0f, cg = color.g / 255.0f;
    const float cb = color.b / 255.0f, ca = color.a / 255.0f;
    const float yo = title ? (float)gTitleYOff : 0.0f;
    float xp = x, yp = y;
    int i = 0;
    const int len = (int)std::strlen(text);
    while (i < len) {
        int cpLen = 0;
        const int cp = GetCodepointNext(&text[i], &cpLen);
        i += cpLen;
        if (cp == '\n') { yp += size; xp = x; continue; }
        const int gi = GetGlyphIndex(f, cp);
        const Rectangle rec = f.recs[gi];
        const GlyphInfo& g = f.glyphs[gi];
        if (cp != ' ' && cp != '\t' && rec.width > 0) {
            const float x0 = xp + g.offsetX * scale, y0 = yp + g.offsetY * scale;
            const float x1 = x0 + rec.width * scale, y1 = y0 + rec.height * scale;
            const float u0 = rec.x / gAtlasW, v0 = (rec.y + yo) / gAtlasH;
            const float u1 = (rec.x + rec.width) / gAtlasW;
            const float v1 = (rec.y + rec.height + yo) / gAtlasH;
            const rdr::UiVert q[6] = {
                { x0, y0, u0, v0, cr, cg, cb, ca }, { x1, y0, u1, v0, cr, cg, cb, ca },
                { x1, y1, u1, v1, cr, cg, cb, ca }, { x0, y0, u0, v0, cr, cg, cb, ca },
                { x1, y1, u1, v1, cr, cg, cb, ca }, { x0, y1, u0, v1, cr, cg, cb, ca },
            };
            rdr::PushUiVerts(q, 6);
        }
        xp += (g.advanceX == 0 ? rec.width * scale : g.advanceX * scale) + spacing;
    }
}

// Drop shadow under every glyph (V35, playtest: "more contrast against the
// background"): a dark offset copy keeps lettering legible over any ground —
// map greens, snowfields, the battle sky. Offset scales with the size; the
// shadow inherits the text's own alpha so faded text fades whole.
static void Shadowed(const Font& f, const char* text, float x, float y,
                     float size, float spacing, Color color, bool title = false) {
    const float off = size >= 30.0f ? 2.0f : 1.0f;
    if (rdr::VulkanUiActive() && EnsureVkAtlas()) {   // the Vulkan road (V173)
        RecordText(f, title, text, x + off, y + off, size, spacing,
                   Fade(BLACK, 0.55f * (color.a / 255.0f)));
        RecordText(f, title, text, x, y, size, spacing, color);
        return;
    }
    DrawTextEx(f, text, { x + off, y + off }, size, spacing,
               Fade(BLACK, 0.55f * (color.a / 255.0f)));
    DrawTextEx(f, text, { x, y }, size, spacing, color);
}

void Text(const char* text, int x, int y, int fontSize, Color color) {
    Shadowed(BodyFont(), text, (float)x, (float)y, Sz(fontSize),
             Spacing(fontSize) * gScale, color);
}

int Measure(const char* text, int fontSize) {
    return (int)MeasureTextEx(BodyFont(), text, Sz(fontSize),
                              Spacing(fontSize) * gScale).x;
}

void Title(const char* text, int x, int y, int fontSize, Color color) {
    Shadowed(TitleFont(), text, (float)x, (float)y, Sz(fontSize),
             Spacing(fontSize) * gScale, color, true);
}

// Solid HUD panels (V174): UV (-1,-1) tells the Vulkan UI shader "no
// texture"; on the GL road it's a plain rectangle.
void RectRec(Rectangle r, Color c) {
    if (rdr::VulkanUiActive()) {
        const float cr = c.r / 255.0f, cg = c.g / 255.0f;
        const float cb = c.b / 255.0f, ca = c.a / 255.0f;
        const float x0 = r.x, y0 = r.y, x1 = r.x + r.width, y1 = r.y + r.height;
        const rdr::UiVert q[6] = {
            { x0, y0, -1, -1, cr, cg, cb, ca }, { x1, y0, -1, -1, cr, cg, cb, ca },
            { x1, y1, -1, -1, cr, cg, cb, ca }, { x0, y0, -1, -1, cr, cg, cb, ca },
            { x1, y1, -1, -1, cr, cg, cb, ca }, { x0, y1, -1, -1, cr, cg, cb, ca },
        };
        rdr::PushUiVerts(q, 6);
        return;
    }
    DrawRectangleRec(r, c);
}

void Rect(int x, int y, int w, int h, Color c) {
    RectRec(Rectangle{ (float)x, (float)y, (float)w, (float)h }, c);
}

// ---- the rest of the 2D vocabulary (V175) ----------------------------------
namespace {
inline rdr::UiVert UV(float x, float y, Color c) {
    return { x, y, -1, -1, c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f };
}
void PushTri(rdr::UiVert a, rdr::UiVert b, rdr::UiVert c) {
    const rdr::UiVert t[3] = { a, b, c };
    rdr::PushUiVerts(t, 3);
}
}  // namespace

void GradientV(int x, int y, int w, int h, Color top, Color bottom) {
    if (!rdr::VulkanUiActive()) { DrawRectangleGradientV(x, y, w, h, top, bottom); return; }
    const rdr::UiVert tl = UV((float)x, (float)y, top), tr = UV((float)(x + w), (float)y, top);
    const rdr::UiVert bl = UV((float)x, (float)(y + h), bottom),
                      br = UV((float)(x + w), (float)(y + h), bottom);
    PushTri(tl, tr, br);
    PushTri(tl, br, bl);
}

void Ring(Vector2 c, float rIn, float rOut, float a0, float a1, int segs, Color col) {
    if (!rdr::VulkanUiActive()) { DrawRing(c, rIn, rOut, a0, a1, segs, col); return; }
    if (segs < 3) segs = 3;
    const float d0 = a0 * DEG2RAD, d1 = a1 * DEG2RAD;
    for (int i = 0; i < segs; ++i) {
        const float t0 = d0 + (d1 - d0) * i / segs, t1 = d0 + (d1 - d0) * (i + 1) / segs;
        const Vector2 i0 = { c.x + sinf(t0) * rIn, c.y - cosf(t0) * rIn };
        const Vector2 i1 = { c.x + sinf(t1) * rIn, c.y - cosf(t1) * rIn };
        const Vector2 o0 = { c.x + sinf(t0) * rOut, c.y - cosf(t0) * rOut };
        const Vector2 o1 = { c.x + sinf(t1) * rOut, c.y - cosf(t1) * rOut };
        PushTri(UV(i0.x, i0.y, col), UV(o0.x, o0.y, col), UV(o1.x, o1.y, col));
        PushTri(UV(i0.x, i0.y, col), UV(o1.x, o1.y, col), UV(i1.x, i1.y, col));
    }
}

void DiscV(Vector2 c, float r, Color col) {
    if (!rdr::VulkanUiActive()) { DrawCircleV(c, r, col); return; }
    Ring(c, 0.0f, r, 0, 360, 24, col);
}

void Disc(int cx, int cy, float r, Color c) { DiscV({ (float)cx, (float)cy }, r, c); }

void DiscGradient(int cx, int cy, float r, Color inner, Color outer) {
    if (!rdr::VulkanUiActive()) { DrawCircleGradient(cx, cy, r, inner, outer); return; }
    const Vector2 c = { (float)cx, (float)cy };
    for (int i = 0; i < 24; ++i) {
        const float t0 = 2 * PI * i / 24, t1 = 2 * PI * (i + 1) / 24;
        PushTri(UV(c.x, c.y, inner),
                UV(c.x + sinf(t0) * r, c.y - cosf(t0) * r, outer),
                UV(c.x + sinf(t1) * r, c.y - cosf(t1) * r, outer));
    }
}

void DiscLines(int cx, int cy, float r, Color c) {
    if (!rdr::VulkanUiActive()) { DrawCircleLines(cx, cy, r, c); return; }
    Ring({ (float)cx, (float)cy }, r - 1.0f, r, 0, 360, 24, c);
}

void RectLinesEx(Rectangle r, float t, Color c) {
    if (!rdr::VulkanUiActive()) { DrawRectangleLinesEx(r, t, c); return; }
    RectRec({ r.x, r.y, r.width, t }, c);
    RectRec({ r.x, r.y + r.height - t, r.width, t }, c);
    RectRec({ r.x, r.y + t, t, r.height - 2 * t }, c);
    RectRec({ r.x + r.width - t, r.y + t, t, r.height - 2 * t }, c);
}

void RectLines(int x, int y, int w, int h, Color c) {
    if (!rdr::VulkanUiActive()) { DrawRectangleLines(x, y, w, h, c); return; }
    RectLinesEx({ (float)x, (float)y, (float)w, (float)h }, 1.0f, c);
}

void Rounded(Rectangle r, float roundness, int segs, Color c) {
    if (!rdr::VulkanUiActive()) { DrawRectangleRounded(r, roundness, segs, c); return; }
    RectRec(r, c);   // the preview squares the corners; native pass restores them
}

void RoundedLines(Rectangle r, float roundness, int segs, Color c) {
    if (!rdr::VulkanUiActive()) { DrawRectangleRoundedLines(r, roundness, segs, c); return; }
    RectLinesEx(r, 1.0f, c);
}

void LineEx(Vector2 a, Vector2 b, float thick, Color c) {
    if (!rdr::VulkanUiActive()) { DrawLineEx(a, b, thick, c); return; }
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;
    const float nx = -dy / len * thick * 0.5f, ny = dx / len * thick * 0.5f;
    PushTri(UV(a.x + nx, a.y + ny, c), UV(b.x + nx, b.y + ny, c), UV(b.x - nx, b.y - ny, c));
    PushTri(UV(a.x + nx, a.y + ny, c), UV(b.x - nx, b.y - ny, c), UV(a.x - nx, a.y - ny, c));
}

void Tri(Vector2 a, Vector2 b, Vector2 c3, Color c) {
    if (!rdr::VulkanUiActive()) { DrawTriangle(a, b, c3, c); return; }
    PushTri(UV(a.x, a.y, c), UV(b.x, b.y, c), UV(c3.x, c3.y, c));
}

int MeasureTitle(const char* text, int fontSize) {
    return (int)MeasureTextEx(TitleFont(), text, Sz(fontSize),
                              Spacing(fontSize) * gScale).x;
}

}  // namespace ui
