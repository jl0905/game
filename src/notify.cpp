#include "notify.h"
#include "ui.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace notify {
namespace {

struct Line {
    std::string text;
    Color c;
    float life, maxLife;
    int size;
};
std::vector<Line> g_lines;   // index 0 = newest (drawn on top)
constexpr int MAX_LINES = 8;   // the log never becomes a wall of text

// Top-left anchor (V204): x flush with the HUD column, y below every
// screen's status block (battle hint lines end ~130, campaign chips ~196).
constexpr int LOG_X = 10, LOG_Y = 200;

}  // namespace

void Push(const char* text, Color c, float life, int size) {
    if (!IsWindowReady() || !text || !text[0]) return;
    g_lines.insert(g_lines.begin(), { text, c, life, life, size });
    if ((int)g_lines.size() > MAX_LINES) g_lines.resize(MAX_LINES);
}

void Kill(const char* killer, const char* victim, Color teamTint) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s cut down %s", killer, victim);
    Push(buf, teamTint, 5.0f, 17);
}

void Clear() { g_lines.clear(); }

void Draw() {
    if (g_lines.empty()) return;
    const float dt = GetFrameTime();
    int y = LOG_Y;
    for (Line& L : g_lines) {   // newest first, stacking downward
        L.life -= dt;
        const float a = L.life < 1.2f ? L.life / 1.2f : 1.0f;   // fade tail
        if (a <= 0.0f) continue;
        const int w = ui::Measure(L.text.c_str(), L.size);
        ui::Rect(LOG_X - 4, y - 3, w + 14, L.size + 8, Fade(BLACK, 0.5f * a));
        ui::Text(L.text.c_str(), LOG_X + 3, y, L.size, Fade(L.c, a));
        y += L.size + 9;
    }
    for (size_t i = 0; i < g_lines.size();)
        if (g_lines[i].life <= 0.0f) g_lines.erase(g_lines.begin() + i);
        else ++i;
}

}  // namespace notify
