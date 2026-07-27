#pragma once

// ---------------------------------------------------------------------------
// Procedural armour skin atlas (V180): armour is a TEXTURE on the body, not
// geometry. One 256x1024 RGBA image, four vertically stacked 256x256
// grayscale tiles — row 0 cloth, 1 leather, 2 chainmail, 3 plate. Patterns
// are brightness modulation around ~0.82 (range ~0.65..1.0) so the instance
// colour (team tint / armour tint) keeps doing the colouring. Row k samples
// with uvRect (0, k*0.25, 1, (k+1)*0.25). Pure pixel math — no raylib types
// in this header; both the GL and Vulkan backends consume the same bytes.
// ---------------------------------------------------------------------------

// Returns the cached atlas pixels (RGBA8, tightly packed) and its size.
const unsigned char* SkinAtlasPixels(int* w, int* h);
