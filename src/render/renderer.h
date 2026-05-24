// renderer.h
#pragma once
#include <vulkan/vulkan.h>
#include "swapchain.h"
#include "vulkan_init.h"
#include "pipeline.h"
#include "text.h"
#include "atlas.h"
#include "solid.h"
#include "font.h"
#include "editor.h"

struct Renderer {
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkFence in_flight;
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    bool swapchain_dirty_local = false;
};

Renderer create_renderer(VulkanDevice& vkdev, GPU& gpu, Pipeline& pipeline);

void render_frame(Renderer& r, VulkanDevice& vkdev, Swapchain& sc,
                  Pipeline& pipeline,
                  TextPipeline& text, Atlas& atlas,
                  SolidPipeline& solid, const AxylFont& font,
                  Editor& editor,
                  float text_origin_x, float text_origin_y);
