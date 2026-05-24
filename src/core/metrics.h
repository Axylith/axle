#pragma once
#include <cstdint>
#include <cstdio>

struct Metrics {
    // EMA smoothing factor (closer to 1 = more smoothing).
    // 0.9 → recent frame counts ~10x more than 100 frames ago.
    static constexpr double ALPHA = 0.9;

    double frame_ms_ema   = 0.0;
    double input_us_ema   = 0.0;   // last keystroke→submit
    uint32_t glyph_count  = 0;
    uint64_t input_count  = 0;
    bool     visible      = true;

    void on_frame(double ms) {
        frame_ms_ema = frame_ms_ema == 0.0
            ? ms
            : ALPHA * frame_ms_ema + (1.0 - ALPHA) * ms;
    }
    void on_input(double us) {
        // No EMA on input latency — show the last value, it's the spiky one.
        input_us_ema = us;
        input_count++;
    }

    // Writes a single line to `out`, max 80 chars. No newline.
    void format(char* out, size_t cap) const {
        snprintf(out, cap,
                 "frame %5.2fms  input %5.0fus  glyphs %u  N=%lu",
                 frame_ms_ema, input_us_ema, glyph_count,
                 (unsigned long)input_count);
    }
};