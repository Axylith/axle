#include "atlas.h"
#include "swapchain.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>


static uint32_t find_memory_type(VkPhysicalDevice physical, uint32_t allowed_types, VkMemoryPropertyFlags properties){
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical, &mem_props);

    for(uint32_t i = 0; i < mem_props.memoryTypeCount; i++){
        bool allowed = (allowed_types & (1 << i)) != 0;
        bool has_props = (mem_props.memoryTypes[i].propertyFlags & properties) == properties;
        if (allowed && has_props){
            return i;
        }
    }
    fprintf(stderr, "[atlas] No suitable memory type found\n");
    return UINT32_MAX;
}


static bool create_buffer(VulkanDevice& vkdev, GPU& gpu, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& out_buffer, VkDeviceMemory& out_memory){
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vkdev.device, &buffer_info, nullptr, &out_buffer) != VK_SUCCESS) {
        fprintf(stderr, "[atlas] vkCreateBuffer failed\n");
        return false;
    }
    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(vkdev.device, out_buffer, &mem_reqs);

    uint32_t mem_type = find_memory_type(gpu.device, mem_reqs.memoryTypeBits, properties);
    if (mem_type == UINT32_MAX) {
        vkDestroyBuffer(vkdev.device, out_buffer, nullptr);
        out_buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = mem_type;

    if (vkAllocateMemory(vkdev.device, &alloc_info, nullptr, &out_memory) != VK_SUCCESS) {
        fprintf(stderr, "[atlas] vkAllocateMemory failed\n");
        vkDestroyBuffer(vkdev.device, out_buffer, nullptr);
        out_buffer = VK_NULL_HANDLE;
        return false;
    }
    
    vkBindBufferMemory(vkdev.device, out_buffer, out_memory, 0);
    return true;


}




Atlas create_atlas(VulkanDevice& vkdev, GPU& gpu, const char* binary_path) {
    Atlas atlas{};

    // --- Phase 1: Load raw RGBA from disk ---

    FILE* f = fopen(binary_path, "rb");
    if (!f) {
        fprintf(stderr, "[atlas] Failed to open: %s\n", binary_path);
        return atlas;
    }

    uint8_t header[12];
    if (fread(header, 1, 12, f) != 12) {
        fprintf(stderr, "[atlas] Failed to read header from %s\n", binary_path);
        fclose(f);
        return atlas;
    }

    if (header[0] != 'R' || header[1] != 'G' ||
        header[2] != 'B' || header[3] != 'A') {
        fprintf(stderr, "[atlas] Bad magic in %s (expected RGBA)\n", binary_path);
        fclose(f);
        return atlas;
    }

    uint32_t width  = ((uint32_t)header[4]  << 24) |
                      ((uint32_t)header[5]  << 16) |
                      ((uint32_t)header[6]  <<  8) |
                       (uint32_t)header[7];
    uint32_t height = ((uint32_t)header[8]  << 24) |
                      ((uint32_t)header[9]  << 16) |
                      ((uint32_t)header[10] <<  8) |
                       (uint32_t)header[11];

    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
        fprintf(stderr, "[atlas] Implausible dimensions %ux%u in %s\n",
                width, height, binary_path);
        fclose(f);
        return atlas;
    }

    size_t pixel_count = (size_t)width * (size_t)height * 4;
    uint8_t* pixels = (uint8_t*)malloc(pixel_count);
    if (!pixels) {
        fprintf(stderr, "[atlas] OOM allocating %zu bytes\n", pixel_count);
        fclose(f);
        return atlas;
    }

    if (fread(pixels, 1, pixel_count, f) != pixel_count) {
        fprintf(stderr, "[atlas] Short read for %s\n", binary_path);
        free(pixels);
        fclose(f);
        return atlas;
    }

    fclose(f);

    printf("[atlas] Loaded %ux%u RGBA from %s (%zu bytes)\n",
           width, height, binary_path, pixel_count);

    // --- Phase 2: Upload pixels to a staging buffer ---

    VkBuffer       staging_buffer;
    VkDeviceMemory staging_memory;

    if (!create_buffer(vkdev, gpu,
                       pixel_count,
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       staging_buffer, staging_memory)) {
        free(pixels);
        return atlas;
    }

    void* mapped_data;
    vkMapMemory(vkdev.device, staging_memory, 0, pixel_count, 0, &mapped_data);
    memcpy(mapped_data, pixels, pixel_count);
    vkUnmapMemory(vkdev.device, staging_memory);

    free(pixels);
    pixels = nullptr;

    printf("[atlas] Staged %zu bytes to host-visible GPU buffer\n", pixel_count);

    // Store dimensions in the struct
    atlas.width = width;
    atlas.height = height;
    atlas.channels = 4;

    // Phase 3 will use staging_buffer to fill a device-local VkImage.
    // For now we return without it, and have a known temporary memory leak.
    VkImageCreateInfo image_info{};
    image_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType     = VK_IMAGE_TYPE_2D;
    image_info.extent.width  = width;
    image_info.extent.height = height;
    image_info.extent.depth  = 1;
    image_info.mipLevels     = 1;
    image_info.arrayLayers   = 1;
    image_info.format        = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                               VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(vkdev.device, &image_info, nullptr, &atlas.image) != VK_SUCCESS) {
        fprintf(stderr, "[atlas] vkCreateImage failed\n");
        vkDestroyBuffer(vkdev.device, staging_buffer, nullptr);
        vkFreeMemory(vkdev.device, staging_memory, nullptr);
        return atlas;  // atlas.image stays VK_NULL_HANDLE — caller knows it failed
    }

    // Query the memory requirements of the image
    VkMemoryRequirements img_mem_reqs;
    vkGetImageMemoryRequirements(vkdev.device, atlas.image, &img_mem_reqs);

    // Find a memory type that's device-local (fast GPU memory)
    uint32_t img_mem_type = find_memory_type(
        gpu.device,
        img_mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    if (img_mem_type == UINT32_MAX) {
        vkDestroyImage(vkdev.device, atlas.image, nullptr);
        atlas.image = VK_NULL_HANDLE;
        vkDestroyBuffer(vkdev.device, staging_buffer, nullptr);
        vkFreeMemory(vkdev.device, staging_memory, nullptr);
        return atlas;
    }

    VkMemoryAllocateInfo img_alloc_info{};
    img_alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    img_alloc_info.allocationSize  = img_mem_reqs.size;
    img_alloc_info.memoryTypeIndex = img_mem_type;

    if (vkAllocateMemory(vkdev.device, &img_alloc_info, nullptr, &atlas.memory) != VK_SUCCESS) {
        fprintf(stderr, "[atlas] vkAllocateMemory for image failed\n");
        vkDestroyImage(vkdev.device, atlas.image, nullptr);
        atlas.image = VK_NULL_HANDLE;
        vkDestroyBuffer(vkdev.device, staging_buffer, nullptr);
        vkFreeMemory(vkdev.device, staging_memory, nullptr);
        return atlas;
    }

    vkBindImageMemory(vkdev.device, atlas.image, atlas.memory, 0);

    printf("[atlas] Image created in device-local memory (%u bytes)\n",
           (uint32_t)img_mem_reqs.size);

    // --- Phase 4: GPU copy from staging buffer to image ---

    // Create a temporary command pool for this one-time upload
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = gpu.graphics_queue_family;

    VkCommandPool upload_pool;
    if (vkCreateCommandPool(vkdev.device, &pool_info, nullptr, &upload_pool) != VK_SUCCESS) {
        fprintf(stderr, "[atlas] Failed to create command pool\n");
        vkDestroyImage(vkdev.device, atlas.image, nullptr);
        vkFreeMemory(vkdev.device, atlas.memory, nullptr);
        atlas.image = VK_NULL_HANDLE;
        atlas.memory = VK_NULL_HANDLE;
        vkDestroyBuffer(vkdev.device, staging_buffer, nullptr);
        vkFreeMemory(vkdev.device, staging_memory, nullptr);
        return atlas;
    }

    // Allocate a one-time command buffer for the upload
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool        = upload_pool;
    alloc_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(vkdev.device, &alloc_info, &cmd) != VK_SUCCESS) {
        fprintf(stderr, "[atlas] Failed to allocate command buffer\n");
        vkDestroyCommandPool(vkdev.device, upload_pool, nullptr);
        vkDestroyImage(vkdev.device, atlas.image, nullptr);
        vkFreeMemory(vkdev.device, atlas.memory, nullptr);
        atlas.image = VK_NULL_HANDLE;
        atlas.memory = VK_NULL_HANDLE;
        vkDestroyBuffer(vkdev.device, staging_buffer, nullptr);
        vkFreeMemory(vkdev.device, staging_memory, nullptr);
        return atlas;
    }


    // Begin recording
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    // Transition 1: UNDEFINED -> TRANSFER_DST_OPTIMAL
    // The image is in UNDEFINED layout (per our create info). We need
    // TRANSFER_DST_OPTIMAL before we can copy into it.
    VkImageMemoryBarrier2 to_transfer{};
    to_transfer.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    to_transfer.srcStageMask  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    to_transfer.srcAccessMask = 0;
    to_transfer.dstStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT;
    to_transfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    to_transfer.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
    to_transfer.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_transfer.image         = atlas.image;
    to_transfer.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    to_transfer.subresourceRange.baseMipLevel   = 0;
    to_transfer.subresourceRange.levelCount     = 1;
    to_transfer.subresourceRange.baseArrayLayer = 0;
    to_transfer.subresourceRange.layerCount     = 1;

    VkDependencyInfo dep_to_transfer{};
    dep_to_transfer.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_to_transfer.imageMemoryBarrierCount = 1;
    dep_to_transfer.pImageMemoryBarriers    = &to_transfer;
    vkCmdPipelineBarrier2(cmd, &dep_to_transfer);

    // Copy buffer -> image
    VkBufferImageCopy region{};
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0;  // 0 = tightly packed, no padding
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(
        cmd,
        staging_buffer,
        atlas.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &region
    );

    // Transition 2: TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
    // After the copy, the image is filled but in transfer layout.
    // We need SHADER_READ_ONLY_OPTIMAL for shader sampling.
    VkImageMemoryBarrier2 to_shader_read{};
    to_shader_read.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    to_shader_read.srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT;
    to_shader_read.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    to_shader_read.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    to_shader_read.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    to_shader_read.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_shader_read.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    to_shader_read.image         = atlas.image;
    to_shader_read.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    to_shader_read.subresourceRange.baseMipLevel   = 0;
    to_shader_read.subresourceRange.levelCount     = 1;
    to_shader_read.subresourceRange.baseArrayLayer = 0;
    to_shader_read.subresourceRange.layerCount     = 1;

    VkDependencyInfo dep_to_shader{};
    dep_to_shader.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_to_shader.imageMemoryBarrierCount = 1;
    dep_to_shader.pImageMemoryBarriers    = &to_shader_read;
    vkCmdPipelineBarrier2(cmd, &dep_to_shader);

    // End recording
    vkEndCommandBuffer(cmd);

    // Submit and wait synchronously
    VkSubmitInfo submit_info{};
    submit_info.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers    = &cmd;

    vkQueueSubmit(vkdev.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(vkdev.graphics_queue);

    // --- Phase 5: Destroy staging buffer and upload command pool ---
    vkDestroyBuffer(vkdev.device, staging_buffer, nullptr);
    vkFreeMemory(vkdev.device, staging_memory, nullptr);

    // Destroying the pool implicitly frees all command buffers in it
    vkDestroyCommandPool(vkdev.device, upload_pool, nullptr);

    printf("[atlas] Pixels uploaded to device-local image\n");

    // --- Phase 6: Create VkImageView and VkSampler ---

    VkImageViewCreateInfo view_info{};
    view_info.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image    = atlas.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format   = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(vkdev.device, &view_info, nullptr, &atlas.view) != VK_SUCCESS) {
        fprintf(stderr, "[atlas] vkCreateImageView failed\n");
        vkDestroyImage(vkdev.device, atlas.image, nullptr);
        vkFreeMemory(vkdev.device, atlas.memory, nullptr);
        atlas.image = VK_NULL_HANDLE;
        atlas.memory = VK_NULL_HANDLE;
        return atlas;
    }

    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter    = VK_FILTER_LINEAR;
    sampler_info.minFilter    = VK_FILTER_LINEAR;
    sampler_info.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.borderColor      = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable    = VK_FALSE;
    sampler_info.compareOp        = VK_COMPARE_OP_ALWAYS;
    sampler_info.mipLodBias       = 0.0f;
    sampler_info.minLod           = 0.0f;
    sampler_info.maxLod           = 0.0f;

    if (vkCreateSampler(vkdev.device, &sampler_info, nullptr, &atlas.sampler) != VK_SUCCESS) {
        fprintf(stderr, "[atlas] vkCreateSampler failed\n");
        vkDestroyImageView(vkdev.device, atlas.view, nullptr);
        atlas.view = VK_NULL_HANDLE;
        vkDestroyImage(vkdev.device, atlas.image, nullptr);
        vkFreeMemory(vkdev.device, atlas.memory, nullptr);
        atlas.image = VK_NULL_HANDLE;
        atlas.memory = VK_NULL_HANDLE;
        return atlas;
    }

    printf("[atlas] Image view and sampler created\n");

    // --- Phase 7: Descriptor set layout, pool, and set ---

    // 1. Layout: declares "set 0, binding 0 is a combined image+sampler"
    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    binding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings    = &binding;

    if (vkCreateDescriptorSetLayout(vkdev.device, &layout_info, nullptr,
                                     &atlas.set_layout) != VK_SUCCESS) {
        fprintf(stderr, "[atlas] vkCreateDescriptorSetLayout failed\n");
        vkDestroySampler(vkdev.device, atlas.sampler, nullptr);
        vkDestroyImageView(vkdev.device, atlas.view, nullptr);
        vkDestroyImage(vkdev.device, atlas.image, nullptr);
        vkFreeMemory(vkdev.device, atlas.memory, nullptr);
        atlas.sampler = VK_NULL_HANDLE;
        atlas.view = VK_NULL_HANDLE;
        atlas.image = VK_NULL_HANDLE;
        atlas.memory = VK_NULL_HANDLE;
        return atlas;
    }

    // 2. Pool: enough storage for 1 combined-image-sampler descriptor in 1 set
    VkDescriptorPoolSize pool_size{};
    pool_size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1;

    VkDescriptorPoolCreateInfo desc_pool_info{};
    desc_pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    desc_pool_info.poolSizeCount = 1;
    desc_pool_info.pPoolSizes    = &pool_size;
    desc_pool_info.maxSets       = 1;
    
    if (vkCreateDescriptorPool(vkdev.device, &desc_pool_info, nullptr, &atlas.pool) != VK_SUCCESS) {
        fprintf(stderr, "[atlas] vkCreateDescriptorPool failed\n");
        vkDestroyDescriptorSetLayout(vkdev.device, atlas.set_layout, nullptr);
        atlas.set_layout = VK_NULL_HANDLE;
        vkDestroySampler(vkdev.device, atlas.sampler, nullptr);
        vkDestroyImageView(vkdev.device, atlas.view, nullptr);
        vkDestroyImage(vkdev.device, atlas.image, nullptr);
        vkFreeMemory(vkdev.device, atlas.memory, nullptr);
        atlas.sampler = VK_NULL_HANDLE;
        atlas.view = VK_NULL_HANDLE;
        atlas.image = VK_NULL_HANDLE;
        atlas.memory = VK_NULL_HANDLE;
        return atlas;
    }

    // 3. Allocate a descriptor set from the pool
    VkDescriptorSetAllocateInfo set_alloc{};
    set_alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_alloc.descriptorPool     = atlas.pool;
    set_alloc.descriptorSetCount = 1;
    set_alloc.pSetLayouts        = &atlas.set_layout;

    if (vkAllocateDescriptorSets(vkdev.device, &set_alloc, &atlas.set) != VK_SUCCESS) {
        fprintf(stderr, "[atlas] vkAllocateDescriptorSets failed\n");
        vkDestroyDescriptorPool(vkdev.device, atlas.pool, nullptr);
        vkDestroyDescriptorSetLayout(vkdev.device, atlas.set_layout, nullptr);
        atlas.pool = VK_NULL_HANDLE;
        atlas.set_layout = VK_NULL_HANDLE;
        vkDestroySampler(vkdev.device, atlas.sampler, nullptr);
        vkDestroyImageView(vkdev.device, atlas.view, nullptr);
        vkDestroyImage(vkdev.device, atlas.image, nullptr);
        vkFreeMemory(vkdev.device, atlas.memory, nullptr);
        atlas.sampler = VK_NULL_HANDLE;
        atlas.view = VK_NULL_HANDLE;
        atlas.image = VK_NULL_HANDLE;
        atlas.memory = VK_NULL_HANDLE;
        return atlas;
    }

    // 4. Point the set at our actual view+sampler
    VkDescriptorImageInfo img_info{};
    img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    img_info.imageView   = atlas.view;
    img_info.sampler     = atlas.sampler;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = atlas.set;
    write.dstBinding      = 0;
    write.dstArrayElement = 0;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo      = &img_info;

    vkUpdateDescriptorSets(vkdev.device, 1, &write, 0, nullptr);

    printf("[atlas] Descriptor set bound to atlas (set=0, binding=0)\n");

    return atlas;
}

void destroy_atlas(VulkanDevice& vkdev, Atlas& atlas) {
    if (atlas.pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vkdev.device, atlas.pool, nullptr);
        // Sets are freed implicitly when the pool is destroyed
        atlas.pool = VK_NULL_HANDLE;
        atlas.set = VK_NULL_HANDLE;
    }
    if (atlas.set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vkdev.device, atlas.set_layout, nullptr);
        atlas.set_layout = VK_NULL_HANDLE;
    }
    if (atlas.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(vkdev.device, atlas.sampler, nullptr);
        atlas.sampler = VK_NULL_HANDLE;
    }
    if (atlas.view != VK_NULL_HANDLE) {
        vkDestroyImageView(vkdev.device, atlas.view, nullptr);
        atlas.view = VK_NULL_HANDLE;
    }
    if (atlas.image != VK_NULL_HANDLE) {
        vkDestroyImage(vkdev.device, atlas.image, nullptr);
        atlas.image = VK_NULL_HANDLE;
    }
    if (atlas.memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkdev.device, atlas.memory, nullptr);
        atlas.memory = VK_NULL_HANDLE;
    }
}

