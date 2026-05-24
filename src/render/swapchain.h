#pragma once
#include <vulkan/vulkan.h>
#include "vulkan_init.h"

struct AppWindow;
struct VulkanState;   // ← add this forward declaration

struct Swapchain {
    VkSwapchainKHR handle;
    VkImage images[4];
    VkImageView views[4];
    uint32_t image_count;
    VkFormat format;
    VkExtent2D extent;
};

Swapchain create_swapchain(VkDevice device, VkPhysicalDevice physical, VkSurfaceKHR surface, AppWindow& app, GPU& gpu, VkSwapchainKHR old_handle = VK_NULL_HANDLE);
void recreate_swapchain(VulkanState& vk, AppWindow& app);
