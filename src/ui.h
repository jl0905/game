#pragma once
#include "raylib.h"

// ---------------------------------------------------------------------------
// Central text rendering. The game draws all of its text through here instead
// of raylib's DrawText/MeasureText so it uses a smooth anti-aliased TTF/OTF
// font rather than the built-in pixelated bitmap font.
//
// Which fonts are used is data-driven and mod-friendly: see assets/fonts.cfg.
// Two roles exist — a "body" font for HUD/menus and a "title" font for
// headings. If a configured font can't be loaded, we fall back to raylib's
// default font, so a bad config never crashes the game.
//
// Lifecycle: call ui::LoadFonts() once after InitWindow(), and
// ui::UnloadFonts() before CloseWindow().
// ---------------------------------------------------------------------------
namespace ui {

void LoadFonts();
void UnloadFonts();

// Drop-in replacements for raylib's DrawText / MeasureText, using the body font.
// Global text scale (U13): applied inside Text/Title AND their measures so
// layouts stay coherent. Default 1.2; fonts.cfg `scale = ...` overrides.
void SetTextScale(float s);

void Text(const char* text, int x, int y, int fontSize, Color color);
int  Measure(const char* text, int fontSize);

// The same, using the title/heading font.
void Title(const char* text, int x, int y, int fontSize, Color color);
int  MeasureTitle(const char* text, int fontSize);

// Screen-space solid panel (V174): records at the renderer seam when the
// Vulkan backend is live (drawn by the Vulkan text/UI pipeline), otherwise
// a plain GL DrawRectangle. Use for HUD panels, bars and hover bands.
void Rect(int x, int y, int w, int h, Color c);
void RectRec(Rectangle r, Color c);
// The rest of the 2D vocabulary (V175) — same contract as Rect: recorded
// as Vulkan UI geometry when the backend is live, raylib GL otherwise.
// Signatures mirror the raylib originals they replace.
void GradientV(int x, int y, int w, int h, Color top, Color bottom);
void Disc(int cx, int cy, float r, Color c);
void DiscV(Vector2 c, float r, Color col);
void DiscGradient(int cx, int cy, float r, Color inner, Color outer);
void DiscLines(int cx, int cy, float r, Color c);
void Ring(Vector2 c, float rIn, float rOut, float a0, float a1, int segs, Color col);
void RectLines(int x, int y, int w, int h, Color c);
void RectLinesEx(Rectangle r, float thick, Color c);
void Rounded(Rectangle r, float roundness, int segs, Color c);
void RoundedLines(Rectangle r, float roundness, int segs, Color c);
void LineEx(Vector2 a, Vector2 b, float thick, Color c);
void Tri(Vector2 a, Vector2 b, Vector2 c3, Color c);
// Direct handles for advanced call sites that want DrawTextEx themselves.
const Font& BodyFont();
const Font& TitleFont();

}  // namespace ui
