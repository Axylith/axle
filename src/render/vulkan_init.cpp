#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xresource.h>
#include <cstdio>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_xlib.h>
#include <chrono>
#include "vulkan_init.h"
#include "window.h"


GPU pick_gpu(VkInstance instance, VkSurfaceKHR surface){
    //how many gpu:
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count,nullptr);
    //First cal with nullptr = "just tell me how many"

    VkPhysicalDevice devices[8]; //8 GPUS max, more than enough
    vkEnumeratePhysicalDevices(instance, &count, devices);

    printf("[vulkan] Found %u GPU(s)\n", count);

    for (uint32_t i = 0; i < count; i++){
        //Get GPU Name
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);
        printf("[vulkan] %u: %s\n", i, props.deviceName);

        //Find queue families
        uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &family_count, nullptr);
        
        VkQueueFamilyProperties families[16];
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &family_count, families);

        GPU gpu{};
        gpu.device = devices[i];
        gpu.graphics_queue_family = UINT32_MAX;
        gpu.present_queue_family = UINT32_MAX;

        for (uint32_t f = 0; f < family_count; f++){
            //Can this family do graphics
            if (families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                gpu.graphics_queue_family = f;
            }

            //Can this family present to our surface?
            VkBool32 can_present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], f, surface, &can_present);
            if(can_present){
                gpu.present_queue_family = f;
            }
        }
    

        // Found a GPU that can do Both? USE IT
        if(gpu.graphics_queue_family != UINT32_MAX && gpu.present_queue_family != UINT32_MAX){
            printf("[vulkan] Using: %s\n", props.deviceName);
            return gpu;
        }
    }

    fprintf(stderr, "[vulkan] No suitable GPU found \n");
    return GPU{};
}

VulkanDevice create_device(GPU& gpu){
    //We need one queue from the graphics family
    float priority = 1.0f;

    VkDeviceQueueCreateInfo queue_info[2]{};
    uint32_t queue_count = 1;

    queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info[0].queueFamilyIndex = gpu.graphics_queue_family;
    queue_info[0].queueCount = 1;
    queue_info[0].pQueuePriorities = &priority;

    // If present is a different family, request it too
    if (gpu.present_queue_family != gpu.graphics_queue_family) {
        queue_info[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info[1].queueFamilyIndex = gpu.present_queue_family;
        queue_info[1].queueCount = 1;
        queue_info[1].pQueuePriorities = &priority;
        queue_count = 2;
    }

    //Enable swapchain extension (required to present to screen)
    const char* extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VkPhysicalDeviceSynchronization2Features sync2{};
    sync2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    sync2.synchronization2 = VK_TRUE;

    VkPhysicalDeviceDynamicRenderingFeatures dyn_rendering{};
    dyn_rendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dyn_rendering.dynamicRendering = VK_TRUE;
    dyn_rendering.pNext = &sync2; // chain them to gether

    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pNext = &dyn_rendering; //Attach chain feature;
    device_info.queueCreateInfoCount = queue_count;
    device_info.pQueueCreateInfos = queue_info;
    device_info.enabledExtensionCount = 1;
    device_info.ppEnabledExtensionNames = extensions;

    VulkanDevice vkdev{};
    VkResult result = vkCreateDevice(gpu.device, &device_info, nullptr, &vkdev.device);
    
    if (result != VK_SUCCESS){
        fprintf(stderr, "[vulkan] Failed to create device: %d\n", result);
        return vkdev;
    }


    //Get Queue handlers
    vkGetDeviceQueue(vkdev.device, gpu.graphics_queue_family, 0, &vkdev.graphics_queue);
    vkGetDeviceQueue(vkdev.device, gpu.present_queue_family, 0, &vkdev.present_queue);

    printf("[vulkan] Device created (dynamic rendering + sync2 enabled)\n");
    return vkdev;



}

VkInstance create_vulkan_instance(){
    printf("[vk-init] step 1: appInfo\n"); fflush(stdout);
    
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "axylith";
    appInfo.applicationVersion = VK_MAKE_VERSION(0,1,0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(0,1,0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    printf("[vk-init] step 2: createInfo\n"); fflush(stdout);
    
    const char* extensions[] = {
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
        VK_KHR_SURFACE_EXTENSION_NAME
    };

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = 2;
    createInfo.ppEnabledExtensionNames = extensions;

    #ifdef NDEBUG
        createInfo.enabledLayerCount = 0;
        createInfo.ppEnabledLayerNames = nullptr;
    #else
        const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = layers;
    #endif

    printf("[vk-init] step 3: calling vkCreateInstance\n"); fflush(stdout);
    
    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);

    printf("[vk-init] step 4: returned %d\n", result); fflush(stdout);
    
    if(result != VK_SUCCESS){
        fprintf(stderr, "[vulkan] failed: %d\n", result);
        fflush(stderr);
        return VK_NULL_HANDLE;
    }

    printf("[vulkan] Instance created\n"); fflush(stdout);
    return instance;
}

VkSurfaceKHR create_surface(VkInstance instance, AppWindow& app){
    VkXlibSurfaceCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    create_info.dpy = app.display;
    create_info.window = app.window;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkResult result = vkCreateXlibSurfaceKHR(instance, &create_info, nullptr, &surface);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] Failed to create surface: %d\n", result);

        return VK_NULL_HANDLE;
    }

    printf("[vulkan] Surface created\n");
    return surface;
}