#include "font.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>


// ─── Mini JSON parser ──────────────────────────────────────────────────────
//
// Hand-rolled, intentionally limited:
//   - Skips whitespace, commas, brackets, braces, quotes
//   - Pulls strings, numbers, ints out of a known structure
//   - No error recovery — assumes msdf-atlas-gen output is well-formed
//   - Throws nothing, just returns false on failure
//
// We use a simple cursor approach: walk the buffer, advance past delimiters,
// extract literal values. This works because we know exactly what shape
// the JSON has — no need for a generic parser.

struct Cursor {
    const char* p;       // current position
    const char* end;     // end of buffer
};

static void skip_ws(Cursor& c) {
    while (c.p < c.end && (*c.p == ' ' || *c.p == '\t' || *c.p == '\n' || *c.p == '\r')) c.p++;
}

// Skip everything up to and including the next character `target`.
// Used to advance past commas, colons, braces.
static bool skip_to(Cursor& c, char target) {
    while (c.p < c.end && *c.p != target) c.p++;
    if (c.p >= c.end) return false;
    c.p++;
    return true;
}

// Find a key like `"width"` (with quotes) within the current scope.
// Returns true if found, leaves cursor right after the key+colon.
// Naive scan — fine for our small JSON.
static bool find_key(Cursor& c, const char* key) {
    size_t key_len = strlen(key);
    while (c.p < c.end) {
        if (*c.p == '"') {
            const char* start = ++c.p;  // skip opening quote
            while (c.p < c.end && *c.p != '"') c.p++;
            if (c.p >= c.end) return false;
            size_t name_len = c.p - start;
            c.p++;  // skip closing quote
            if (name_len == key_len && memcmp(start, key, key_len) == 0) {
                skip_ws(c);
                if (c.p < c.end && *c.p == ':') c.p++;
                skip_ws(c);
                return true;
            }
        } else {
            c.p++;
        }
    }
    return false;
}

// Read a numeric value (int or float) at current cursor.
// Returns the parsed value as double; advances cursor past it.
static double parse_number(Cursor& c) {
    char* end;
    double v = strtod(c.p, &end);
    c.p = end;
    return v;
}

// Read an integer at current cursor.
static int parse_int(Cursor& c) {
    char* end;
    long v = strtol(c.p, &end, 10);
    c.p = end;
    return (int)v;
}

// Read a substructure { ... } and return cursor positions for its content.
// Handles nesting correctly.
static bool find_object_bounds(Cursor& c, const char*& obj_start, const char*& obj_end) {
    skip_ws(c);
    if (c.p >= c.end || *c.p != '{') return false;
    obj_start = c.p + 1;
    int depth = 1;
    c.p++;
    while (c.p < c.end && depth > 0) {
        if (*c.p == '{') depth++;
        else if (*c.p == '}') depth--;
        if (depth > 0) c.p++;
    }
    if (depth != 0) return false;
    obj_end = c.p;
    c.p++;  // skip closing brace
    return true;
}

// Read an array [ ... ] — same pattern as object, but for arrays.
static bool find_array_bounds(Cursor& c, const char*& arr_start, const char*& arr_end) {
    skip_ws(c);
    if (c.p >= c.end || *c.p != '[') return false;
    arr_start = c.p + 1;
    int depth = 1;
    c.p++;
    while (c.p < c.end && depth > 0) {
        if (*c.p == '[') depth++;
        else if (*c.p == ']') depth--;
        if (depth > 0) c.p++;
    }
    if (depth != 0) return false;
    arr_end = c.p;
    c.p++;
    return true;
}

// ─── Reading specific structures ───────────────────────────────────────────

// Read the four bounds: left, bottom, right, top
// Either planeBounds or atlasBounds — same structure.
static void parse_bounds(Cursor& obj, float& l, float& b, float& r, float& t) {
    Cursor c = obj;
    if (find_key(c, "left"))   l = (float)parse_number(c);
    c = obj;
    if (find_key(c, "bottom")) b = (float)parse_number(c);
    c = obj;
    if (find_key(c, "right"))  r = (float)parse_number(c);
    c = obj;
    if (find_key(c, "top"))    t = (float)parse_number(c);
}

static void parse_glyph(Cursor& obj, Glyph& g) {
    Cursor c = obj;

    if (find_key(c, "unicode")) g.codepoint = (uint32_t)parse_int(c);

    c = obj;
    if (find_key(c, "advance")) g.advance = (float)parse_number(c);

    c = obj;
    if (find_key(c, "planeBounds")) {
        const char *plane_start, *plane_end;
        if (find_object_bounds(c, plane_start, plane_end)) {
            Cursor plane = { plane_start, plane_end };
            parse_bounds(plane, g.plane_left, g.plane_bottom, g.plane_right, g.plane_top);
        }
    }

    c = obj;
    if (find_key(c, "atlasBounds")) {
        const char *atlas_start, *atlas_end;
        if (find_object_bounds(c, atlas_start, atlas_end)) {
            Cursor atlas = { atlas_start, atlas_end };
            parse_bounds(atlas,
                         g.atlas_left, g.atlas_bottom,
                         g.atlas_right, g.atlas_top);
        }
    }
}

// ─── Public API ────────────────────────────────────────────────────────────

bool font_load_metadata(AxylFont& font, const char* json_path) {
    // Read file into memory
    FILE* f = fopen(json_path, "rb");
    if (!f) {
        fprintf(stderr, "[font] Failed to open %s\n", json_path);
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "[font] fseek end failed for %s\n", json_path);
        fclose(f);
        return false;
    }

    long ftold = ftell(f);
    if (ftold < 0) {
        fprintf(stderr, "[font] ftell failed for %s: %s\n",
                json_path, strerror(errno));
        fclose(f);
        return false;
    }
    if (ftold == 0) {
        fprintf(stderr, "[font] %s is empty\n", json_path);
        fclose(f);
        return false;
    }
    // Sanity cap. JetBrains Mono metadata is ~30KB; anything 16MB+
    // is either wrong or malicious.
    if (ftold > 16 * 1024 * 1024) {
        fprintf(stderr, "[font] %s is unreasonably large (%ld bytes)\n",
                json_path, ftold);
        fclose(f);
        return false;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "[font] fseek start failed for %s\n", json_path);
        fclose(f);
        return false;
    }

    size_t size = (size_t)ftold;
    char* buf = (char*)malloc(size + 1);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "[font] malloc(%zu) failed\n", size + 1);
        return false;
    }

    size_t got = fread(buf, 1, size, f);
    fclose(f);
    if (got != size) {
        fprintf(stderr, "[font] short read for %s: got %zu of %zu\n",
                json_path, got, size);
        free(buf);
        return false;
    }
    buf[size] = '\0';

    memset(&font, 0, sizeof(font));

    Cursor root = { buf, buf + size };

    // ── atlas { width, height, distanceRange, size } ──
    {
        Cursor c = root;
        if (find_key(c, "atlas")) {
            const char *atlas_start, *atlas_end;
            if (find_object_bounds(c, atlas_start, atlas_end)) {
                Cursor a = { atlas_start, atlas_end };
                Cursor t = a;
                if (find_key(t, "width"))         font.atlas_width = (uint32_t)parse_int(t);
                t = a;
                if (find_key(t, "height"))        font.atlas_height = (uint32_t)parse_int(t);
                t = a;
                if (find_key(t, "distanceRange")) font.distance_range = (float)parse_number(t);
                t = a;
                if (find_key(t, "size"))          font.em_size_px = (float)parse_number(t);
            }
        }
    }

    // ── metrics { lineHeight, ascender, descender, underlineY, underlineThickness } ──
    {
        Cursor c = root;
        if (find_key(c, "metrics")) {
            const char *m_start, *m_end;
            if (find_object_bounds(c, m_start, m_end)) {
                Cursor m = { m_start, m_end };
                Cursor t = m;
                if (find_key(t, "lineHeight"))         font.line_height = (float)parse_number(t);
                t = m;
                if (find_key(t, "ascender"))           font.ascender = (float)parse_number(t);
                t = m;
                if (find_key(t, "descender"))          font.descender = (float)parse_number(t);
                t = m;
                if (find_key(t, "underlineY"))         font.underline_y = (float)parse_number(t);
                t = m;
                if (find_key(t, "underlineThickness")) font.underline_thickness = (float)parse_number(t);
            }
        }
    }

    // ── glyphs [ {...}, {...}, ... ] ──
    {
        Cursor c = root;
        if (find_key(c, "glyphs")) {
            const char *arr_start, *arr_end;
            if (find_array_bounds(c, arr_start, arr_end)) {
                Cursor a = { arr_start, arr_end };

                while (a.p < a.end) {
                    skip_ws(a);
                    if (a.p >= a.end) break;
                    if (*a.p == ',') { a.p++; continue; }
                    if (*a.p != '{') break;

                    const char *g_start, *g_end;
                    if (!find_object_bounds(a, g_start, g_end)) break;

                    Glyph g{};
                    Cursor obj = { g_start, g_end };
                    parse_glyph(obj, g);

                    // Only store ASCII glyphs in our 128-entry table
                    if (g.codepoint < 128) {
                        font.glyphs[g.codepoint] = g;
                    }
                }
            }
        }
    }

    free(buf);

    // Validation: make sure we got the basics
    if (font.atlas_width == 0 || font.atlas_height == 0) {
        fprintf(stderr, "[font] Atlas dimensions missing or zero\n");
        return false;
    }
    if (font.em_size_px == 0) {
        fprintf(stderr, "[font] Em size missing or zero\n");
        return false;
    }

    printf("[font] Loaded %s: %ux%u atlas, %.0fpx em, %d glyphs\n",
           json_path, font.atlas_width, font.atlas_height, font.em_size_px,
           // Count populated glyphs
           [&font]() {
               int n = 0;
               for (int i = 0; i < 128; i++) if (font.glyphs[i].advance > 0) n++;
               return n;
           }());

    return true;
}

const Glyph* font_get_glyph(const AxylFont& font, uint32_t codepoint) {
    if (codepoint >= 128) return nullptr;
    if (font.glyphs[codepoint].advance == 0 && codepoint != ' ') return nullptr;
    return &font.glyphs[codepoint];
}

bool font_compute_quad(const Glyph& glyph,
                        float& cursor_x, float baseline_y, float pixel_size,
                        float& x0, float& y0, float& x1, float& y1) {
    // planeBounds are in em units. Multiply by pixel_size to get pixels.
    // Atlas y-origin is bottom (we set yOrigin in atlas options); we flip here
    // because screen y increases downward.

    // Whitespace glyph (no quad to draw): just advance the cursor.
    if (glyph.plane_left == glyph.plane_right) {
        cursor_x += glyph.advance * pixel_size;
        return false;
    }

    x0 = cursor_x + glyph.plane_left  * pixel_size;
    x1 = cursor_x + glyph.plane_right * pixel_size;

    // Plane Y: bottom is below baseline, top is above.
    // Screen Y increases downward, so:
    //   screen_y_top    = baseline_y - plane_top * pixel_size
    //   screen_y_bottom = baseline_y - plane_bottom * pixel_size
    y0 = baseline_y - glyph.plane_top    * pixel_size;
    y1 = baseline_y - glyph.plane_bottom * pixel_size;

    cursor_x += glyph.advance * pixel_size;
    return true;
}