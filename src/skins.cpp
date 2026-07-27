#include "skins.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// The armour skin atlas (V180). Each tile is a 256x256 grayscale pattern
// written as RGBA; brightness modulates the instance colour in the shaders,
// so cloth stays team-coloured cloth and plate stays steel-tinted plate.

namespace {

constexpr int TILE = 256;
constexpr int ATLAS_W = 256;
constexpr int ATLAS_H = 1024;

// Small integer hash -> [0,1). Deterministic across platforms/backends.
float Hash2(int x, int y) {
    unsigned h = (unsigned)(x * 374761393) ^ (unsigned)(y * 668265263u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFF) / 65536.0f;
}

// Row 0 — cloth: a soft woven checker with thread-level shimmer.
float Cloth(int x, int y) {
    const int cx = (x / 8) & 1, cy = (y / 8) & 1;
    const float weave = (cx ^ cy) ? 0.045f : -0.045f;
    const float thread = 0.03f * sinf(x * 0.9f) * sinf(y * 0.9f);
    return 0.82f + weave + thread;
}

// Row 1 — leather: mottled patches with stitch lines every 64px.
float Leather(int x, int y) {
    const float patch = 0.72f + 0.18f * Hash2(x / 16, y / 16);
    const int sy = y % 64, sx = x % 64;
    const bool stitch = (sy < 2) || (sx < 2);
    float b = patch;
    if (stitch) b = 0.65f;
    else if ((sy >= 2 && sy < 4) || (sx >= 2 && sx < 4)) b = fminf(b + 0.08f, 1.0f);
    return b;
}

// Row 2 — chainmail: dense ring speckle on a staggered grid.
float Mail(int x, int y) {
    const int ry = y / 6;
    const int xo = (ry & 1) ? 3 : 0;
    const float dx = (float)((x + xo) % 6) - 2.5f;
    const float dy = (float)(y % 6) - 2.5f;
    const float d = sqrtf(dx * dx + dy * dy);
    float b = 0.74f;
    if (d > 1.4f && d < 2.6f) b = 0.93f;            // the ring
    else if (d <= 1.4f) b = 0.68f;                  // its shadowed eye
    return b + 0.04f * (Hash2(x, y) - 0.5f);        // metal sparkle
}

// Row 3 — plate: broad horizontal bands with edge shadows and rivets.
float Plate(int x, int y) {
    const int by = y % 64;
    float b = (by < 52) ? 0.90f : 0.70f;            // band face / lapped edge
    if (by >= 48 && by < 52) b = 0.96f;             // highlight above the lap
    const int rx = x % 64, ry = y % 64;
    const float dx = (float)rx - 32.0f, dy = (float)ry - 26.0f;
    if (dx * dx + dy * dy < 9.0f) b = 1.0f;         // rivet head
    else if (dx * dx + dy * dy < 16.0f) b = 0.66f;  // rivet ring shadow
    return b;
}

std::vector<unsigned char> g_atlas;

}  // namespace

const unsigned char* SkinAtlasPixels(int* w, int* h) {
    if (g_atlas.empty()) {
        g_atlas.resize((size_t)ATLAS_W * ATLAS_H * 4);
        for (int y = 0; y < ATLAS_H; ++y)
            for (int x = 0; x < ATLAS_W; ++x) {
                const int row = y / TILE, ty = y % TILE;
                float b;
                switch (row) {
                    case 0:  b = Cloth(x, ty); break;
                    case 1:  b = Leather(x, ty); break;
                    case 2:  b = Mail(x, ty); break;
                    default: b = Plate(x, ty); break;
                }
                b = b < 0.6f ? 0.6f : b > 1.0f ? 1.0f : b;
                const unsigned char v = (unsigned char)(b * 255.0f);
                unsigned char* p = &g_atlas[((size_t)y * ATLAS_W + x) * 4];
                p[0] = p[1] = p[2] = v;
                p[3] = 255;
            }
    }
    if (w) *w = ATLAS_W;
    if (h) *h = ATLAS_H;
    return g_atlas.data();
}
