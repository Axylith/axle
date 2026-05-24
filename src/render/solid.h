#pragma once

#include <vulkan/vulkan.h>
#include "vulkan_init.h"

// A solid-color quad pipeline. No texture, no SDF — just colored triangles
// in screen-pixel space. Used for: cursor, selection highlights, UI panels,
// dividers, toolbar backgrounds, anything that's a flat-colored rectangle.
//
// Coordinate system matches the text pipeline: (0,0) top-left, pixels,
// Y growing downward. The vertex shader converts to NDC.

// One vertex: pos(vec2) + color(vec4) = 6 floats = 24 bytes.
struct SolidVertex {
    float pos[2];
    float color[4];
};

struct SolidPipeline {
    VkPipeline       handle = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;

    // Vertex buffer holding this frame's quads. Refilled each frame.
    VkBuffer       vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
    void*          vertex_mapped = nullptr;  // persistent mapping

    uint32_t max_quads  = 0;   // capacity
    uint32_t quad_count = 0;   // quads in the buffer right now
};

// Push constants. Must match solid.vert layout.
struct SolidPushConstants {
    float screen_size[2];
    float _padding[2];
};

// Build the pipeline + allocate the vertex buffer.
SolidPipeline create_solid_pipeline(VkDevice device,
                                    VkFormat color_format,
                                    GPU& gpu,
                                    uint32_t max_quads);

void destroy_solid_pipeline(VkDevice device, SolidPipeline& sp);

// Reset the quad buffer to empty. Call once per frame before appending.
void solid_begin(SolidPipeline& sp);

// Append one axis-aligned rectangle in pixel space.
// (x, y) is the top-left corner; w, h are width/height in pixels.
// Returns false if the buffer is full.
bool solid_push_rect(SolidPipeline& sp,
                     float x, float y, float w, float h,
                     float r, float g, float b, float a);

// Issue the draw call. Must be called inside vkCmdBeginRendering /
// vkCmdEndRendering. Draw order matters: call this BEFORE render_text
// if you want rectangles behind the text (e.g. selection highlight),
// or AFTER for rectangles on top (e.g. a modal overlay).
void render_solid(VkCommandBuffer cmd,
                  SolidPipeline& sp,
                  uint32_t screen_width,
                  uint32_t screen_height);