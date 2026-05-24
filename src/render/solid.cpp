#include "solid.h"
#include "pipeline.h"   // for exe_relative
#include <cstdio>
#include <cstdlib>
#include <cstring>

// --- helpers ---

static uint32_t find_memory_type_solid(VkPhysicalDevice physical,
                                        uint32_t allowed_types,
                                        VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((allowed_types & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    fprintf(stderr, "[solid] No suitable memory type\n");
    return UINT32_MAX;
}

static uint32_t* load_spirv_solid(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[solid] Failed to open %s\n", path);
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint32_t* buf = (uint32_t*)malloc(*size);
    if (buf) {
        size_t got = fread(buf, 1, *size, f);
        if (got != *size) {
            fprintf(stderr, "[solid] Short read on %s\n", path);
            free(buf);
            fclose(f);
            return nullptr;
        }
    }
    fclose(f);
    return buf;
}

// --- main: create the solid pipeline ---

SolidPipeline create_solid_pipeline(VkDevice device,
                                    VkFormat color_format,
                                    GPU& gpu,
                                    uint32_t max_quads) {
    SolidPipeline sp{};
    sp.max_quads = max_quads;

    // --- Load shaders ---
    size_t vert_size, frag_size;
    uint32_t* vert_code = load_spirv_solid(exe_relative("shaders/solid.vert.spv"), &vert_size);
    uint32_t* frag_code = load_spirv_solid(exe_relative("shaders/solid.frag.spv"), &frag_size);
    if (!vert_code || !frag_code) {
        free(vert_code); free(frag_code);
        return sp;
    }

    VkShaderModuleCreateInfo vert_module_info{};
    vert_module_info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vert_module_info.codeSize = vert_size;
    vert_module_info.pCode    = vert_code;

    VkShaderModuleCreateInfo frag_module_info{};
    frag_module_info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    frag_module_info.codeSize = frag_size;
    frag_module_info.pCode    = frag_code;

    VkShaderModule vert_module, frag_module;
    vkCreateShaderModule(device, &vert_module_info, nullptr, &vert_module);
    vkCreateShaderModule(device, &frag_module_info, nullptr, &frag_module);
    free(vert_code);
    free(frag_code);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_module;
    stages[0].pName  = "main";

    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_module;
    stages[1].pName  = "main";

    // --- Vertex input: SolidVertex = pos(vec2) + color(vec4) ---
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(SolidVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].binding = 0; attrs[0].location = 0;
    attrs[0].format  = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset  = offsetof(SolidVertex, pos);
    attrs[1].binding = 0; attrs[1].location = 1;
    attrs[1].format  = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[1].offset  = offsetof(SolidVertex, color);

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount   = 1;
    vertex_input.pVertexBindingDescriptions      = &binding;
    vertex_input.vertexAttributeDescriptionCount = 2;
    vertex_input.pVertexAttributeDescriptions    = attrs;

    // --- Input assembly, viewport, raster, multisample (all standard) ---
    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth   = 1.0f;
    raster.cullMode    = VK_CULL_MODE_NONE;
    raster.frontFace   = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // --- Color blending: standard alpha blend (so translucent highlights work) ---
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable         = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp        = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend.alphaBlendOp        = VK_BLEND_OP_ADD;
    blend.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend{};
    color_blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments    = &blend;

    // --- Dynamic viewport + scissor ---
    VkDynamicState dynamics[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates    = dynamics;

    // --- Pipeline layout: NO descriptor sets, just push constants ---
    VkPushConstantRange pc_range{};
    pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pc_range.offset     = 0;
    pc_range.size       = sizeof(SolidPushConstants);

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount         = 0;
    layout_info.pSetLayouts            = nullptr;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges    = &pc_range;

    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &sp.layout) != VK_SUCCESS) {
        fprintf(stderr, "[solid] vkCreatePipelineLayout failed\n");
        vkDestroyShaderModule(device, vert_module, nullptr);
        vkDestroyShaderModule(device, frag_module, nullptr);
        return sp;
    }

    // --- Dynamic rendering format ---
    VkPipelineRenderingCreateInfo rendering_info{};
    rendering_info.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering_info.colorAttachmentCount    = 1;
    rendering_info.pColorAttachmentFormats = &color_format;

    // --- Pipeline ---
    VkGraphicsPipelineCreateInfo pipe_info{};
    pipe_info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipe_info.pNext               = &rendering_info;
    pipe_info.stageCount          = 2;
    pipe_info.pStages             = stages;
    pipe_info.pVertexInputState   = &vertex_input;
    pipe_info.pInputAssemblyState = &input_assembly;
    pipe_info.pViewportState      = &viewport_state;
    pipe_info.pRasterizationState = &raster;
    pipe_info.pMultisampleState   = &multisample;
    pipe_info.pColorBlendState    = &color_blend;
    pipe_info.pDynamicState       = &dynamic_state;
    pipe_info.layout              = sp.layout;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipe_info,
                                   nullptr, &sp.handle) != VK_SUCCESS) {
        fprintf(stderr, "[solid] vkCreateGraphicsPipelines failed\n");
        vkDestroyPipelineLayout(device, sp.layout, nullptr);
        sp.layout = VK_NULL_HANDLE;
        vkDestroyShaderModule(device, vert_module, nullptr);
        vkDestroyShaderModule(device, frag_module, nullptr);
        return sp;
    }

    vkDestroyShaderModule(device, vert_module, nullptr);
    vkDestroyShaderModule(device, frag_module, nullptr);

    // --- Vertex buffer: persistent host-visible mapping ---
    // 6 vertices per quad (two triangles).
    VkDeviceSize buffer_size = (VkDeviceSize)max_quads * 6 * sizeof(SolidVertex);

    VkBufferCreateInfo buf_info{};
    buf_info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size        = buffer_size;
    buf_info.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &buf_info, nullptr, &sp.vertex_buffer) != VK_SUCCESS) {
        fprintf(stderr, "[solid] vkCreateBuffer failed\n");
        vkDestroyPipeline(device, sp.handle, nullptr);
        vkDestroyPipelineLayout(device, sp.layout, nullptr);
        sp.handle = VK_NULL_HANDLE;
        sp.layout = VK_NULL_HANDLE;
        return sp;
    }

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device, sp.vertex_buffer, &mem_reqs);

    uint32_t mem_type = find_memory_type_solid(
        gpu.device, mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mem_type == UINT32_MAX) {
        vkDestroyBuffer(device, sp.vertex_buffer, nullptr);
        vkDestroyPipeline(device, sp.handle, nullptr);
        vkDestroyPipelineLayout(device, sp.layout, nullptr);
        sp.vertex_buffer = VK_NULL_HANDLE;
        sp.handle = VK_NULL_HANDLE;
        sp.layout = VK_NULL_HANDLE;
        return sp;
    }

    VkMemoryAllocateInfo mem_alloc{};
    mem_alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mem_alloc.allocationSize  = mem_reqs.size;
    mem_alloc.memoryTypeIndex = mem_type;

    if (vkAllocateMemory(device, &mem_alloc, nullptr, &sp.vertex_memory) != VK_SUCCESS) {
        fprintf(stderr, "[solid] vkAllocateMemory failed\n");
        vkDestroyBuffer(device, sp.vertex_buffer, nullptr);
        vkDestroyPipeline(device, sp.handle, nullptr);
        vkDestroyPipelineLayout(device, sp.layout, nullptr);
        sp.vertex_buffer = VK_NULL_HANDLE;
        sp.handle = VK_NULL_HANDLE;
        sp.layout = VK_NULL_HANDLE;
        return sp;
    }

    vkBindBufferMemory(device, sp.vertex_buffer, sp.vertex_memory, 0);

    if (vkMapMemory(device, sp.vertex_memory, 0, buffer_size, 0,
                    &sp.vertex_mapped) != VK_SUCCESS) {
        fprintf(stderr, "[solid] vkMapMemory failed\n");
        vkFreeMemory(device, sp.vertex_memory, nullptr);
        vkDestroyBuffer(device, sp.vertex_buffer, nullptr);
        vkDestroyPipeline(device, sp.handle, nullptr);
        vkDestroyPipelineLayout(device, sp.layout, nullptr);
        return sp;
    }

    printf("[solid] Pipeline created (max %u quads, vertex buffer %zu KB)\n",
           max_quads, (size_t)(buffer_size / 1024));
    return sp;
}

// --- vertex buffer filling ---

void solid_begin(SolidPipeline& sp) {
    sp.quad_count = 0;
}

bool solid_push_rect(SolidPipeline& sp,
                     float x, float y, float w, float h,
                     float r, float g, float b, float a) {
    if (!sp.vertex_mapped || sp.quad_count >= sp.max_quads) {
        return false;
    }

    float x0 = x;
    float y0 = y;
    float x1 = x + w;
    float y1 = y + h;

    SolidVertex* verts = (SolidVertex*)sp.vertex_mapped;
    SolidVertex* v = &verts[sp.quad_count * 6];

    auto set = [&](SolidVertex& sv, float px, float py) {
        sv.pos[0] = px; sv.pos[1] = py;
        sv.color[0] = r; sv.color[1] = g; sv.color[2] = b; sv.color[3] = a;
    };

    // Two triangles, same winding as the text pipeline's glyph quads.
    set(v[0], x0, y0);
    set(v[1], x1, y0);
    set(v[2], x0, y1);
    set(v[3], x1, y0);
    set(v[4], x1, y1);
    set(v[5], x0, y1);

    sp.quad_count++;
    return true;
}

// --- draw ---

void render_solid(VkCommandBuffer cmd,
                  SolidPipeline& sp,
                  uint32_t screen_width,
                  uint32_t screen_height) {
    if (sp.quad_count == 0 || sp.handle == VK_NULL_HANDLE) {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sp.handle);

    SolidPushConstants pc{};
    pc.screen_size[0] = (float)screen_width;
    pc.screen_size[1] = (float)screen_height;
    pc._padding[0]    = 0.0f;
    pc._padding[1]    = 0.0f;
    vkCmdPushConstants(cmd,
                       sp.layout,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(pc), &pc);

    VkBuffer buffers[] = { sp.vertex_buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);

    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = (float)screen_width;
    viewport.height   = (float)screen_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent.width  = screen_width;
    scissor.extent.height = screen_height;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // 6 vertices per quad.
    vkCmdDraw(cmd, sp.quad_count * 6, 1, 0, 0);
}

void destroy_solid_pipeline(VkDevice device, SolidPipeline& sp) {
    if (sp.vertex_mapped) {
        if (sp.vertex_memory != VK_NULL_HANDLE) {
            vkUnmapMemory(device, sp.vertex_memory);
        }
        sp.vertex_mapped = nullptr;
    }
    if (sp.vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, sp.vertex_buffer, nullptr);
        sp.vertex_buffer = VK_NULL_HANDLE;
    }
    if (sp.vertex_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, sp.vertex_memory, nullptr);
        sp.vertex_memory = VK_NULL_HANDLE;
    }
    if (sp.handle != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, sp.handle, nullptr);
        sp.handle = VK_NULL_HANDLE;
    }
    if (sp.layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, sp.layout, nullptr);
        sp.layout = VK_NULL_HANDLE;
    }
}