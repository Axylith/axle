#pragma once

#include <vulkan/vulkan.h>
#include "vulkan_init.h"
#include "atlas.h"
#include "font.h"
#include "solid.h"

// One vertex in a text quad. 8 floats per vertex = 32 bytes.
//   pos:   screen-space pixel coordinates
//   uv:    atlas texture coordinates [0, 1]
//   color: RGBA, 0-1 per channel
struct TextVertex {
    float pos[2];
    float uv[2];
    float color[4];
};

// The text rendering pipeline + its persistent GPU resources.
struct TextPipeline {
    VkPipeline       handle = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;

    // Vertex buffer that holds the current frame's glyph quads.
    // Sized for some maximum number of glyphs; we refill it each frame.
    VkBuffer         vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory   vertex_memory = VK_NULL_HANDLE;
    void*            vertex_mapped = nullptr;  // persistent mapping

    uint32_t         max_glyphs    = 0;
    uint32_t         glyph_count   = 0;  // glyphs in the buffer right now

    float pen_x      = 0.0f;
    float baseline_y = 0.0f;
    float pixel_size = 0.0f;
    float cursor_pen_x      = 0.0f;
    float cursor_baseline_y = 0.0f;

};

// Push constants sent each draw. Must match text.vert/text.frag layout.
struct TextPushConstants {
    float screen_size[2];   // viewport pixel size
    float pixel_range;      // MTSDF pxrange (4.0 from our baking)
    float _padding;
};

// Build a graphics pipeline that consumes TextVertex and samples the atlas.
TextPipeline create_text_pipeline(VkDevice device,
                                  VkFormat color_format,
                                  VkDescriptorSetLayout atlas_layout,
                                  GPU& gpu,
                                  uint32_t max_glyphs);

void destroy_text_pipeline(VkDevice device, TextPipeline& tp);

// Fill the vertex buffer for one string. Replaces any prior content.
// Returns the number of glyphs emitted (text.glyph_count after the call).
uint32_t build_text_vertices(TextPipeline& tp,
                              const AxylFont& font,
                              const char* utf8_text,
                              float origin_x_px,
                              float origin_y_px,
                              float pixel_size,        // size in screen pixels
                              float r, float g, float b, float a);

// Issue the draw commands. Must be called between vkCmdBeginRendering
// and vkCmdEndRendering, after the main quad has been drawn.
void render_text(VkCommandBuffer cmd,
                 TextPipeline& tp,
                 Atlas& atlas,
                 uint32_t screen_width,
                 uint32_t screen_height,
                 float pixel_range);


bool append_cursor_quad(TextPipeline& tp,
                        const AxylFont& font,
                        float r, float g, float b, float a);


uint32_t append_text_run(TextPipeline& tp,
                         const AxylFont& font,
                         const char* utf8_text,
                         float origin_x_px,
                         float origin_y_px,
                         float pixel_size,
                         float r, float g, float b, float a);


uint32_t build_text_vertices_with_cursor(TextPipeline& tp,
                                          const AxylFont& font,
                                          const char* utf8_text,
                                          size_t cursor_byte_offset,
                                          float origin_x_px,
                                          float origin_y_px,
                                          float pixel_size,
                                          float r, float g, float b, float a);

bool append_cursor_quad(TextPipeline& tp,
                        const AxylFont& font,
                        float r, float g, float b, float a);

uint32_t emit_selection_rects(SolidPipeline& sp,
                              const AxylFont& font,
                              const char* utf8_text,
                              size_t sel_lo,
                              size_t sel_hi,
                              float origin_x_px,
                              float origin_y_px,
                              float pixel_size,
                              float r, float g, float b, float a);