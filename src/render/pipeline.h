#pragma once
#include <vulkan/vulkan.h>

struct Pipeline {
    VkPipelineLayout layout;
    VkPipeline handle;
};

Pipeline create_pipeline(VkDevice device, VkFormat color_format);
const char* exe_relative(const char* relpath);