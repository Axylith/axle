#pragma once
#include <cstdint>

// One glyph's data: how to draw it from the atlas.
// All numeric fields are in em units (font's natural scale).
// Multiply by pixel_size to get actual screen pixels.
struct Glyph {
    uint32_t codepoint;     // Unicode value, e.g. 'A' = 65
    float advance;          // distance to next glyph baseline-to-baseline

    // Plane bounds: where to draw the quad relative to baseline + cursor x
    // (em units; multiply by pixel_size to get pixels)
    float plane_left, plane_bottom, plane_right, plane_top;

    // Atlas bounds: where the glyph's pixels are in the atlas
    // (UV coordinates 0-1)
    float atlas_left, atlas_bottom, atlas_right, atlas_top;
};

struct AxylFont {
    uint32_t atlas_width;
    uint32_t atlas_height;
    float distance_range;
    float em_size_px;

    float line_height;
    float ascender;
    float descender;
    float underline_y;
    float underline_thickness;

    Glyph glyphs[128];
};

bool font_load_metadata(AxylFont& font, const char* json_path);
const Glyph* font_get_glyph(const AxylFont& font, uint32_t codepoint);
bool font_compute_quad(const Glyph& glyph,
                       float& cursor_x, float baseline_y, float pixel_size,
                       float& x0, float& y0, float& x1, float& y1);