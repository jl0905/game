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
void Text(const char* text, int x, int y, int fontSize, Color color);
int  Measure(const char* text, int fontSize);

// The same, using the title/heading font.
void Title(const char* text, int x, int y, int fontSize, Color color);
int  MeasureTitle(const char* text, int fontSize);

// Direct handles for advanced call sites that want DrawTextEx themselves.
const Font& BodyFont();
const Font& TitleFont();

}  // namespace ui
