#include "manualview.h"
#include "rdr.h"
#include "ui.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// The manual screen. Windowed-only overlay: it is never reachable headless
// (the harness has no F1), so — as a deliberate exception to the
// Gather/Update/Draw split — it reads the devices directly. No simulation
// state is touched beyond gs.screen.

namespace {

struct ManualLine {
    int kind;            // 0 body, 1 section heading, 2 blank
    std::string text;
};

struct ManualPage {
    std::string title;
    std::vector<ManualLine> lines;
};

std::vector<ManualPage> g_pages;
bool   g_loaded = false;
int    g_page = 0;
float  g_scroll = 0.0f;      // pixels, per current page
Screen g_prev = Screen::Campaign;

std::string ManualPath() {
    const std::string candidates[] = {
        std::string(GetApplicationDirectory()) + "assets/manual.txt",
        "assets/manual.txt", "../assets/manual.txt" };
    for (const std::string& p : candidates)
        if (FileExists(p.c_str())) return p;
    return "";
}

void LoadManual() {
    g_loaded = true;
    g_pages.clear();
    const std::string path = ManualPath();
    std::ifstream f(path);
    std::string line;
    while (f && std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("# ", 0) == 0) {
            g_pages.push_back({ line.substr(2), {} });
            continue;
        }
        if (g_pages.empty()) continue;   // preamble before the first page
        if (line.rfind("## ", 0) == 0)
            g_pages.back().lines.push_back({ 1, line.substr(3) });
        else if (line.empty())
            g_pages.back().lines.push_back({ 2, "" });
        else
            g_pages.back().lines.push_back({ 0, line });
    }
    if (g_pages.empty())
        g_pages.push_back({ "Manual",
                            { { 0, "assets/manual.txt is missing - reinstall or"
                                   " restore it to read the manual here." } } });
}

// Word-wrap one body line into rows no wider than `width` at `size`.
std::vector<std::string> Wrap(const std::string& text, int width, int size) {
    std::vector<std::string> rows;
    std::istringstream ss(text);
    std::string word, row;
    while (ss >> word) {
        const std::string trial = row.empty() ? word : row + " " + word;
        if (!row.empty() && ui::Measure(trial.c_str(), size) > width) {
            rows.push_back(row);
            row = word;
        } else {
            row = trial;
        }
    }
    if (!row.empty()) rows.push_back(row);
    return rows;
}

}  // namespace

void ManualViewOpen(GameState& gs) {
    if (!g_loaded) LoadManual();
    g_prev = gs.screen;
    g_scroll = 0.0f;
    gs.screen = Screen::ManualView;
}

void ManualViewUpdate(GameState& gs) {
    if (!g_loaded) LoadManual();
    const int n = (int)g_pages.size();

    // Close: Esc or F1 again.
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_F1)) {
        gs.screen = g_prev;
        return;
    }
    // Page turn: arrows / PgUp / PgDn.
    int turn = 0;
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_PAGE_DOWN)) turn = 1;
    if (IsKeyPressed(KEY_LEFT)  || IsKeyPressed(KEY_PAGE_UP))   turn = -1;

    // Mouse: tabs across the top, prev/next buttons in the footer.
    const int W = GetScreenWidth(), H = GetScreenHeight();
    const Vector2 m = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        // Tabs (same layout the draw uses).
        int tx = W / 2 - 340;
        const int ty = 118, th = 30;
        for (int i = 0; i < n; ++i) {
            const int tw = ui::Measure(g_pages[i].title.c_str(), 18) + 18;
            if (m.x >= tx && m.x <= tx + tw && m.y >= ty && m.y <= ty + th) {
                g_page = i;
                g_scroll = 0;
            }
            tx += tw + 6;
        }
        // Footer arrows.
        const int fy = H - 54;
        if (m.y >= fy && m.y <= fy + 34) {
            if (m.x >= W / 2 - 200 && m.x <= W / 2 - 80)  turn = -1;
            if (m.x >= W / 2 + 80  && m.x <= W / 2 + 200) turn = 1;
        }
        // Close button, top-right.
        if (m.x >= W - 120 && m.y <= 90) { gs.screen = g_prev; return; }
    }
    if (turn != 0) {
        g_page = (g_page + turn + n) % n;
        g_scroll = 0;
    }
    // Scroll the page: wheel or Up/Down held.
    g_scroll -= GetMouseWheelMove() * 60.0f;
    if (IsKeyDown(KEY_DOWN)) g_scroll += 6.0f;
    if (IsKeyDown(KEY_UP))   g_scroll -= 6.0f;
    if (g_scroll < 0) g_scroll = 0;
}

void ManualViewDraw(const GameState& gs) {
    (void)gs;
    if (!g_loaded) LoadManual();
    const int W = GetScreenWidth(), H = GetScreenHeight();
    const int n = (int)g_pages.size();
    if (g_page >= n) g_page = 0;
    const ManualPage& page = g_pages[g_page];

    BeginDrawing();
    ClearBackground(Color{ 22, 24, 29, 255 });

    // Header.
    const int x0 = W / 2 - 340;
    ui::Title("THE MANUAL", x0, 44, 40, GOLD);
    ui::Text("everything a captain can do, and what it does", x0 + 300, 58, 18,
             Fade(RAYWHITE, 0.6f));
    // Close chip.
    const Vector2 m = GetMousePosition();
    const bool overClose = m.x >= W - 120 && m.y <= 90;
    ui::Rect(W - 116, 48, 92, 30, Fade(overClose ? GOLD : DARKGRAY, 0.35f));
    ui::Text(overClose ? "close  Esc" : "close", W - 104, 53, 18, RAYWHITE);

    // Page tabs.
    {
        int tx = x0;
        const int ty = 118, th = 30;
        for (int i = 0; i < n; ++i) {
            const int tw = ui::Measure(g_pages[i].title.c_str(), 18) + 18;
            const bool cur = i == g_page;
            const bool hov = m.x >= tx && m.x <= tx + tw && m.y >= ty && m.y <= ty + th;
            ui::Rect(tx, ty, tw, th,
                     cur ? Fade(GOLD, 0.30f) : Fade(DARKGRAY, hov ? 0.45f : 0.22f));
            ui::Text(g_pages[i].title.c_str(), tx + 9, ty + 5, 18,
                     cur ? GOLD : RAYWHITE);
            tx += tw + 6;
        }
    }

    // Body: word-wrapped, scrolled, clipped by simple culling.
    const int bodyX = x0;
    const int bodyW = W - bodyX * 2 < 480 ? W - 40 : W - bodyX * 2;
    const int top = 170, bottom = H - 66;
    int y = top - (int)g_scroll;
    for (const ManualLine& ln : page.lines) {
        if (ln.kind == 2) { y += 12; continue; }
        if (ln.kind == 1) {
            y += 10;
            if (y > top - 40 && y < bottom)
                ui::Title(ln.text.c_str(), bodyX, y, 24, Color{ 235, 200, 120, 255 });
            y += 38;
            continue;
        }
        for (const std::string& row : Wrap(ln.text, bodyW, 19)) {
            if (y > top - 30 && y < bottom)
                ui::Text(row.c_str(), bodyX, y, 19, RAYWHITE);
            y += 26;
        }
    }
    // Fade band so scrolled text doesn't collide with the header visually.
    ui::Rect(0, 0, W, top - 14, Fade(Color{ 22, 24, 29, 255 }, 0.0f));   // no-op keeps layout honest

    // Footer: prev / position / next.
    {
        const int fy = H - 54;
        const bool hp = m.y >= fy && m.y <= fy + 34 && m.x >= W / 2 - 200 && m.x <= W / 2 - 80;
        const bool hn = m.y >= fy && m.y <= fy + 34 && m.x >= W / 2 + 80  && m.x <= W / 2 + 200;
        ui::Rect(W / 2 - 200, fy, 120, 34, Fade(hp ? GOLD : DARKGRAY, 0.30f));
        ui::Text(hp ? "< prev  [Left]" : "< prev", W / 2 - 188, fy + 7, 18, RAYWHITE);
        ui::Rect(W / 2 + 80, fy, 120, 34, Fade(hn ? GOLD : DARKGRAY, 0.30f));
        ui::Text(hn ? "next >  [Right]" : "next >", W / 2 + 96, fy + 7, 18, RAYWHITE);
        ui::Text(TextFormat("%d / %d", g_page + 1, n), W / 2 - 24, fy + 7, 18,
                 Fade(RAYWHITE, 0.7f));
        ui::Text("wheel / Up-Down scrolls    F1 or Esc returns to the game",
                 x0, H - 88, 16, Fade(RAYWHITE, 0.45f));
    }

    rdr::PresentVulkanUi();   // Vulkan HUD composite (V173 pattern)
    EndDrawing();
}
