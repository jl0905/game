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
};
std::vector<Line> g_lines;
constexpr int MAX_LINES = 6;   // the rail never becomes a wall of text

}  // namespace

void Push(const char* text, Color c, float life) {
    if (!IsWindowReady() || !text || !text[0]) return;
    g_lines.push_back({ text, c, life, life });
    if ((int)g_lines.size() > MAX_LINES)
        g_lines.erase(g_lines.begin(), g_lines.end() - MAX_LINES);
}

void Kill(const char* killer, const char* victim, Color teamTint) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s cut down %s", killer, victim);
    Push(buf, teamTint, 3.0f);
}

void Clear() { g_lines.clear(); }

void Draw() {
    if (g_lines.empty()) return;
    const float dt = GetFrameTime();
    const int W = GetScreenWidth(), H = GetScreenHeight();
    // Stack upward from just above the bottom bar; newest line lowest.
    int y = H - 64;
    for (int i = (int)g_lines.size() - 1; i >= 0; --i) {
        Line& L = g_lines[i];
        L.life -= dt;
        const float a = L.life < 0.8f ? L.life / 0.8f : 1.0f;   // fade tail
        if (a <= 0.0f) continue;
        const int fs = 20;
        const int w = ui::Measure(L.text.c_str(), fs);
        ui::Rect(W / 2 - w / 2 - 10, y - 3, w + 20, fs + 8,
                 Fade(BLACK, 0.55f * a));
        ui::Text(L.text.c_str(), W / 2 - w / 2, y, fs, Fade(L.c, a));
        y -= fs + 12;
    }
    for (size_t i = 0; i < g_lines.size();)
        if (g_lines[i].life <= 0.0f) g_lines.erase(g_lines.begin() + i);
        else ++i;
}

}  // namespace notify
