#include "swapchain.h"
#include "window.h"
#include "pipeline.h"
#include "vulkan_init.h"
#include "types.h"
#include <cstdio>


Swapchain create_swapchain(VkDevice device, VkPhysicalDevice physical, VkSurfaceKHR surface, AppWindow& app, GPU& gpu, VkSwapchainKHR old_handle){
    //Query what the surface supports
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, nullptr);
    VkSurfaceFormatKHR formats[16];
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, formats);

    VkSurfaceFormatKHR chosen_format = formats[0]; //fallback
    for (uint32_t i =0; i < format_count; i++){
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR){
            chosen_format = formats[i];
            break;
        }
    }


    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

    uint32_t mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &mode_count, nullptr);
    VkPresentModeKHR modes[16];
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &mode_count, modes);

    for(uint32_t i = 0; i < mode_count; i++){
        if(modes[i] == VK_PRESENT_MODE_MAILBOX_KHR){
            present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        }
    }

    if(present_mode == VK_PRESENT_MODE_FIFO_KHR){
        for(uint32_t i = 0; i < mode_count; i++){
            if(modes[i] == VK_PRESENT_MODE_FIFO_RELAXED_KHR){
                present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
                break;
            }
        }
    }

    //Pick extent (size)
    VkExtent2D extent;
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    } else {
        extent.width = app.width;
        extent.height = app.height;
    }

    // Pick image count (double or triple buffering)
    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = surface;
    create_info.minImageCount = image_count;
    create_info.imageFormat = chosen_format.format;
    create_info.imageColorSpace = chosen_format.colorSpace;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create_info.preTransform = caps.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = old_handle;

    uint32_t family_indices[] = { gpu.graphics_queue_family, gpu.present_queue_family};

    if (gpu.graphics_queue_family != gpu.present_queue_family){
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = family_indices;
    } else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    Swapchain sc{};
    sc.format = chosen_format.format;
    sc.extent = extent;

    VkResult result = vkCreateSwapchainKHR(device, &create_info, nullptr, &sc.handle);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] Failed to create swapchain: %d\n", result);
        return sc;
    }

    // Get swapchain images
    vkGetSwapchainImagesKHR(device, sc.handle, &sc.image_count, nullptr);
    vkGetSwapchainImagesKHR(device, sc.handle, &sc.image_count, sc.images);

    for (uint32_t i = 0; i < sc.image_count; i++){
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = sc.images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = sc.format;
        view_info.components = { VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY };
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;
        
        vkCreateImageView(device, &view_info, nullptr, &sc.views[i]);
                            
    }

    printf("[vulkan] Swapchain created (%ux%u %u images)\n", extent.width, extent.height, sc.image_count);
    return sc;
}

void recreate_swapchain(VulkanState& vk, AppWindow& app){
    if (app.width == 0 || app.height == 0) return;

    vkDeviceWaitIdle(vk.vkdev.device);

    // Defer destruction of any pending old swapchain from a previous recreate
    if (vk.pending_destroy_swapchain != VK_NULL_HANDLE) {
        for (uint32_t i = 0; i < vk.pending_destroy_count; i++) {
            vkDestroyImageView(vk.vkdev.device, vk.pending_destroy_views[i], nullptr);
        }
        vkDestroySwapchainKHR(vk.vkdev.device, vk.pending_destroy_swapchain, nullptr);
        vk.pending_destroy_swapchain = VK_NULL_HANDLE;
        vk.pending_destroy_count = 0;
    }

    // Save current as pending — DO NOT destroy
    VkSwapchainKHR old_handle = vk.swapchain.handle;
    vk.pending_destroy_swapchain = old_handle;
    vk.pending_destroy_count = vk.swapchain.image_count;
    for (uint32_t i = 0; i < vk.swapchain.image_count; i++) {
        vk.pending_destroy_views[i] = vk.swapchain.views[i];
    }

    Swapchain new_sc = create_swapchain(vk.vkdev.device, vk.gpu.device,
                                        vk.surface, app, vk.gpu, old_handle);
    if (new_sc.handle == VK_NULL_HANDLE) {
        fprintf(stderr, "[vulkan] Swapchain recreation failed\n");
        return;
    }
    vk.swapchain = new_sc;

    vkDestroySemaphore(vk.vkdev.device, vk.renderer.image_available, nullptr);
    vkDestroySemaphore(vk.vkdev.device, vk.renderer.render_finished, nullptr);
    VkSemaphoreCreateInfo sem_info{};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(vk.vkdev.device, &sem_info, nullptr, &vk.renderer.image_available);
    vkCreateSemaphore(vk.vkdev.device, &sem_info, nullptr, &vk.renderer.render_finished);
}