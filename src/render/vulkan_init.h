#pragma once
#include <vulkan/vulkan.h>

struct AppWindow;

struct GPU {
    VkPhysicalDevice device;
    uint32_t graphics_queue_family;
    uint32_t present_queue_family;
};

struct VulkanDevice {
    VkDevice device;
    VkQueue graphics_queue;
    VkQueue present_queue;  
};


VkInstance create_vulkan_instance();
VkSurfaceKHR create_surface(VkInstance instance, AppWindow& app);
GPU pick_gpu(VkInstance instance, VkSurfaceKHR surface);
VulkanDevice create_device(GPU& gpu);