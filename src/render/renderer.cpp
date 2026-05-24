#include "renderer.h"
#include "text.h"
#include "atlas.h"
#include <cstdio>
#include <cstring>

// Find memory type that matches requirements
static uint32_t find_memory_type(VkPhysicalDevice physical, uint32_t type_filter,
                                  VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    fprintf(stderr, "[vulkan] Failed to find suitable memory type\n");
    return 0;
}

Renderer create_renderer(VulkanDevice& vkdev, GPU& gpu, Pipeline& pipeline) {
    Renderer r{};
    float r1 = 0.102f; float g1 = 0.094f; float b1 = 0.086f; float a1 = 1.0f;

    // Command pool
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = gpu.graphics_queue_family;
    vkCreateCommandPool(vkdev.device, &pool_info, nullptr, &r.command_pool);

    // Command buffer
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = r.command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    vkAllocateCommandBuffers(vkdev.device, &alloc_info, &r.command_buffer);

    // Sync objects
    VkSemaphoreCreateInfo sem_info{};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    vkCreateSemaphore(vkdev.device, &sem_info, nullptr, &r.image_available);
    vkCreateSemaphore(vkdev.device, &sem_info, nullptr, &r.render_finished);
    vkCreateFence(vkdev.device, &fence_info, nullptr, &r.in_flight);

    // Vertex buffer — one quad (two triangles, 6 vertices)
    // Each vertex: x, y, r, g, b, a
    float vertices[] = {
        // Triangle 1 (top-left half)
        // x      y      r     g     b     a
        -0.8f, -1.0f,  r1, g1, b1, a1,  // top-left     (Tol teal #00767B)
         1.0f, -1.0f,  r1, g1, b1, a1,  // top-right
        -0.8f,  1.0f,  r1, g1, b1, a1,  // bottom-left

        // Triangle 2 (bottom-right half)
         1.0f, -1.0f,  r1, g1, b1, a1,  // top-right
         1.0f,  1.0f,  r1, g1, b1, a1,  // bottom-right
        -0.8f,  1.0f,  r1, g1, b1, a1,  // bottom-left
    };

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = sizeof(vertices);
    buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(vkdev.device, &buffer_info, nullptr, &r.vertex_buffer);

    // Allocate memory for the buffer
    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(vkdev.device, r.vertex_buffer, &mem_reqs);

    VkMemoryAllocateInfo mem_alloc{};
    mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mem_alloc.allocationSize = mem_reqs.size;
    mem_alloc.memoryTypeIndex = find_memory_type(
        gpu.device, mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    vkAllocateMemory(vkdev.device, &mem_alloc, nullptr, &r.vertex_memory);
    vkBindBufferMemory(vkdev.device, r.vertex_buffer, r.vertex_memory, 0);

    // Copy vertex data into the buffer
    void* data;
    vkMapMemory(vkdev.device, r.vertex_memory, 0, sizeof(vertices), 0, &data);
    memcpy(data, vertices, sizeof(vertices));
    vkUnmapMemory(vkdev.device, r.vertex_memory);

    printf("[vulkan] Renderer created (command pool + sync + vertex buffer)\n");
    return r;
}

void render_frame(Renderer& r, VulkanDevice& vkdev, Swapchain& sc,
                  Pipeline& pipeline,
                  TextPipeline& text, Atlas& atlas,
                  SolidPipeline& solid, const AxylFont& font,
                  Editor& editor,
                  float text_origin_x, float text_origin_y) {
    // 1. Wait for previous frame
    vkWaitForFences(vkdev.device, 1, &r.in_flight, VK_TRUE, UINT64_MAX);

    
    // 2. Get next swapchain image
    uint32_t image_index;
    VkResult acq = vkAcquireNextImageKHR(vkdev.device, sc.handle, UINT64_MAX,
                                         r.image_available, VK_NULL_HANDLE, &image_index);
    

    if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
        r.swapchain_dirty_local = true;
        return;
    }

    if (acq == VK_NOT_READY || acq == VK_TIMEOUT){
        return;
    }
    
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
        fprintf(stderr, "[vulkan] vkAcquireNextImageKHR failed: %d\n", acq);
        return;
    }
    
    vkResetFences(vkdev.device, 1, &r.in_flight);


    // 3. Record commands
    vkResetCommandBuffer(r.command_buffer, 0);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(r.command_buffer, &begin_info);

    // Transition to color attachment
    VkImageMemoryBarrier2 barrier_to_render{};
    barrier_to_render.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier_to_render.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    barrier_to_render.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier_to_render.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier_to_render.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier_to_render.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier_to_render.image = sc.images[image_index];
    barrier_to_render.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier_to_render.subresourceRange.levelCount = 1;
    barrier_to_render.subresourceRange.layerCount = 1;

    VkDependencyInfo dep_to_render{};
    dep_to_render.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_to_render.imageMemoryBarrierCount = 1;
    dep_to_render.pImageMemoryBarriers = &barrier_to_render;
    vkCmdPipelineBarrier2(r.command_buffer, &dep_to_render);

    // Begin dynamic rendering
    VkRenderingAttachmentInfo color_attachment{};
    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView = sc.views[image_index];
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.clearValue.color = {{0.055f, 0.051f, 0.047f, 1.0f}};

    VkRenderingInfo rendering_info{};
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.extent = sc.extent;
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &color_attachment;

    vkCmdBeginRendering(r.command_buffer, &rendering_info);

    // Bind pipeline
    vkCmdBindPipeline(r.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle);

    // Set viewport
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)sc.extent.width;
    viewport.height = (float)sc.extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(r.command_buffer, 0, 1, &viewport);

    // Set scissor
    VkRect2D scissor{};
    scissor.extent = sc.extent;
    vkCmdSetScissor(r.command_buffer, 0, 1, &scissor);

    // Bind vertex buffer and draw
    VkBuffer buffers[] = {r.vertex_buffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(r.command_buffer, 0, 1, buffers, offsets);
    vkCmdDraw(r.command_buffer, 6, 1, 0, 0);  // 6 vertices = 1 quad

    // --- Solid pass: selection highlights, behind the text ---
    solid_begin(solid);
    if (editor.has_selection) {
        size_t sel_lo, sel_hi;
        editor_selection_range(editor, sel_lo, sel_hi);
        fprintf(stderr, "[sel] lo=%zu hi=%zu origin=(%.1f,%.1f)\n",
        sel_lo, sel_hi, text_origin_x, text_origin_y);
        emit_selection_rects(solid, font, editor.text.c_str(),
                     sel_lo, sel_hi,
                     text_origin_x,
                     text_origin_y,
                     18.0f,
                     0.66f, 0.69f, 0.71f, 0.30f);
    }
    render_solid(r.command_buffer, solid, sc.extent.width, sc.extent.height);




        // NEW: draw text on top of the teal square
    render_text(r.command_buffer, text, atlas,
                sc.extent.width, sc.extent.height,
                4.0f);  // pxrange we baked with

    vkCmdEndRendering(r.command_buffer);

    // Transition to present
    VkImageMemoryBarrier2 barrier_to_present{};
    barrier_to_present.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier_to_present.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier_to_present.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier_to_present.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    barrier_to_present.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier_to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier_to_present.image = sc.images[image_index];
    barrier_to_present.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier_to_present.subresourceRange.levelCount = 1;
    barrier_to_present.subresourceRange.layerCount = 1;

    VkDependencyInfo dep_to_present{};
    dep_to_present.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_to_present.imageMemoryBarrierCount = 1;
    dep_to_present.pImageMemoryBarriers = &barrier_to_present;
    vkCmdPipelineBarrier2(r.command_buffer, &dep_to_present);

    vkEndCommandBuffer(r.command_buffer);

    // 4. Submit
    VkSemaphore wait_semaphores[] = {r.image_available};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signal_semaphores[] = {r.render_finished};

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &r.command_buffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    vkQueueSubmit(vkdev.graphics_queue, 1, &submit_info, r.in_flight);

    // 5. Present
    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &sc.handle;
    present_info.pImageIndices = &image_index;

    VkResult pres = vkQueuePresentKHR(vkdev.present_queue, &present_info);
    if(pres == VK_ERROR_OUT_OF_DATE_KHR){
        r.swapchain_dirty_local = true;
    } else if (pres != VK_SUCCESS && pres != VK_SUBOPTIMAL_KHR){
        fprintf(stderr, "[vulkan] vkQueuePresentKHR failed: %d\n", pres);
    }
}