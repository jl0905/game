#include "ui.h"
#include <cstdlib>
#include <cstring>
#include <string>

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

void Text(const char* text, int x, int y, int fontSize, Color color) {
    DrawTextEx(BodyFont(), text, { (float)x, (float)y }, Sz(fontSize),
               Spacing(fontSize) * gScale, color);
}

int Measure(const char* text, int fontSize) {
    return (int)MeasureTextEx(BodyFont(), text, Sz(fontSize),
                              Spacing(fontSize) * gScale).x;
}

void Title(const char* text, int x, int y, int fontSize, Color color) {
    DrawTextEx(TitleFont(), text, { (float)x, (float)y }, Sz(fontSize),
               Spacing(fontSize) * gScale, color);
}

int MeasureTitle(const char* text, int fontSize) {
    return (int)MeasureTextEx(TitleFont(), text, Sz(fontSize),
                              Spacing(fontSize) * gScale).x;
}

}  // namespace ui
