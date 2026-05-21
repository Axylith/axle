#pragma once

#include <vulkan/vulkan.h>
#include "vulkan_init.h"

struct Atlas {
    // GPU resources
    VkImage        image   = VK_NULL_HANDLE;
    VkDeviceMemory memory  = VK_NULL_HANDLE;
    VkImageView    view    = VK_NULL_HANDLE;
    VkSampler      sampler = VK_NULL_HANDLE;

    // Descriptor binding plumbing
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkDescriptorPool      pool       = VK_NULL_HANDLE;
    VkDescriptorSet       set        = VK_NULL_HANDLE;

    // Dimensions (read from file)
    uint32_t width    = 0;
    uint32_t height   = 0;
    uint32_t channels = 4;
};

Atlas create_atlas(VulkanDevice& vkdev, GPU& gpu, const char* binary_path);
void destroy_atlas(VulkanDevice& vkdev, Atlas& atlas);

