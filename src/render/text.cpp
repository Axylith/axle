#include "text.h"
#include "pipeline.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// --- helpers ---

static uint32_t find_memory_type_text(VkPhysicalDevice physical,
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
    fprintf(stderr, "[text] No suitable memory type\n");
    return UINT32_MAX;
}

static uint32_t* load_spirv_text(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[text] Failed to open %s\n", path);
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint32_t* buf = (uint32_t*)malloc(*size);
    fread(buf, 1, *size, f);
    fclose(f);
    return buf;
}

// --- main: create the text pipeline ---

TextPipeline create_text_pipeline(VkDevice device,
                                   VkFormat color_format,
                                   VkDescriptorSetLayout atlas_layout,
                                   GPU& gpu,
                                   uint32_t max_glyphs) {
    TextPipeline tp{};
    tp.max_glyphs = max_glyphs;

    // --- Load shaders ---
    size_t vert_size, frag_size;
    uint32_t* vert_code = load_spirv_text(exe_relative("shaders/text.vert.spv"), &vert_size);
    uint32_t* frag_code = load_spirv_text(exe_relative("shaders/text.frag.spv"), &frag_size);
    if (!vert_code || !frag_code) {
        free(vert_code); free(frag_code);
        return tp;
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

    // --- Vertex input: TextVertex = pos(vec2) + uv(vec2) + color(vec4) ---
    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(TextVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0].binding = 0; attrs[0].location = 0;
    attrs[0].format  = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset  = offsetof(TextVertex, pos);
    attrs[1].binding = 0; attrs[1].location = 1;
    attrs[1].format  = VK_FORMAT_R32G32_SFLOAT;
    attrs[1].offset  = offsetof(TextVertex, uv);
    attrs[2].binding = 0; attrs[2].location = 2;
    attrs[2].format  = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[2].offset  = offsetof(TextVertex, color);

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount   = 1;
    vertex_input.pVertexBindingDescriptions      = &binding;
    vertex_input.vertexAttributeDescriptionCount = 3;
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

    // --- Color blending: alpha blend on, premultiplied alpha source ---
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

    // --- Pipeline layout: descriptor set 0 (atlas) + push constants ---
    VkPushConstantRange pc_range{};
    pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pc_range.offset     = 0;
    pc_range.size       = sizeof(TextPushConstants);

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount         = 1;
    layout_info.pSetLayouts            = &atlas_layout;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges    = &pc_range;

    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &tp.layout) != VK_SUCCESS) {
        fprintf(stderr, "[text] vkCreatePipelineLayout failed\n");
        vkDestroyShaderModule(device, vert_module, nullptr);
        vkDestroyShaderModule(device, frag_module, nullptr);
        return tp;
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
    pipe_info.layout              = tp.layout;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipe_info,
                                   nullptr, &tp.handle) != VK_SUCCESS) {
        fprintf(stderr, "[text] vkCreateGraphicsPipelines failed\n");
        vkDestroyPipelineLayout(device, tp.layout, nullptr);
        tp.layout = VK_NULL_HANDLE;
        vkDestroyShaderModule(device, vert_module, nullptr);
        vkDestroyShaderModule(device, frag_module, nullptr);
        return tp;
    }

    vkDestroyShaderModule(device, vert_module, nullptr);
    vkDestroyShaderModule(device, frag_module, nullptr);

    // --- Vertex buffer: persistent host-visible mapping ---
    VkDeviceSize buffer_size = (VkDeviceSize)max_glyphs * 6 * sizeof(TextVertex);

    VkBufferCreateInfo buf_info{};
    buf_info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size        = buffer_size;
    buf_info.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &buf_info, nullptr, &tp.vertex_buffer) != VK_SUCCESS) {
        fprintf(stderr, "[text] vkCreateBuffer failed\n");
        vkDestroyPipeline(device, tp.handle, nullptr);
        vkDestroyPipelineLayout(device, tp.layout, nullptr);
        tp.handle = VK_NULL_HANDLE;
        tp.layout = VK_NULL_HANDLE;
        return tp;
    }

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device, tp.vertex_buffer, &mem_reqs);

    uint32_t mem_type = find_memory_type_text(
        gpu.device, mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mem_type == UINT32_MAX) {
        vkDestroyBuffer(device, tp.vertex_buffer, nullptr);
        vkDestroyPipeline(device, tp.handle, nullptr);
        vkDestroyPipelineLayout(device, tp.layout, nullptr);
        tp.vertex_buffer = VK_NULL_HANDLE;
        tp.handle = VK_NULL_HANDLE;
        tp.layout = VK_NULL_HANDLE;
        return tp;
    }

    VkMemoryAllocateInfo mem_alloc{};
    mem_alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mem_alloc.allocationSize  = mem_reqs.size;
    mem_alloc.memoryTypeIndex = mem_type;

    if (vkAllocateMemory(device, &mem_alloc, nullptr, &tp.vertex_memory) != VK_SUCCESS) {
        fprintf(stderr, "[text] vkAllocateMemory failed\n");
        vkDestroyBuffer(device, tp.vertex_buffer, nullptr);
        vkDestroyPipeline(device, tp.handle, nullptr);
        vkDestroyPipelineLayout(device, tp.layout, nullptr);
        tp.vertex_buffer = VK_NULL_HANDLE;
        tp.handle = VK_NULL_HANDLE;
        tp.layout = VK_NULL_HANDLE;
        return tp;
    }

    vkBindBufferMemory(device, tp.vertex_buffer, tp.vertex_memory, 0);

    // Persistent mapping — keep the pointer for the lifetime of the buffer
    if (vkMapMemory(device, tp.vertex_memory, 0, buffer_size, 0,
                    &tp.vertex_mapped) != VK_SUCCESS) {
        fprintf(stderr, "[text] vkMapMemory failed\n");
        vkFreeMemory(device, tp.vertex_memory, nullptr);
        vkDestroyBuffer(device, tp.vertex_buffer, nullptr);
        vkDestroyPipeline(device, tp.handle, nullptr);
        vkDestroyPipelineLayout(device, tp.layout, nullptr);
        return tp;
    }

    printf("[text] Pipeline created (max %u glyphs, vertex buffer %zu KB)\n",
           max_glyphs, (size_t)(buffer_size / 1024));
    return tp;
}


void render_text(VkCommandBuffer cmd,
                 TextPipeline& tp,
                 Atlas& atlas,
                 uint32_t screen_width,
                 uint32_t screen_height,
                 float pixel_range) {
    if (tp.glyph_count == 0 || tp.handle == VK_NULL_HANDLE) {
        return;
    }

    // Bind the text pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tp.handle);

    // Bind the atlas descriptor set at set 0
    vkCmdBindDescriptorSets(cmd,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            tp.layout,
                            0, 1, &atlas.set,
                            0, nullptr);

    // Push constants: screen size + pxrange
    TextPushConstants pc{};
    pc.screen_size[0] = (float)screen_width;
    pc.screen_size[1] = (float)screen_height;
    pc.pixel_range    = pixel_range;
    pc._padding       = 0.0f;
    vkCmdPushConstants(cmd,
                       tp.layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);

    // Bind the vertex buffer
    VkBuffer buffers[] = { tp.vertex_buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);

    // Set viewport and scissor to match screen
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

    // Draw: 6 vertices per glyph
    vkCmdDraw(cmd, tp.glyph_count * 6, 1, 0, 0);
}
void destroy_text_pipeline(VkDevice device, TextPipeline& tp) {
    if (tp.vertex_mapped) {
        if (tp.vertex_memory != VK_NULL_HANDLE) {
            vkUnmapMemory(device, tp.vertex_memory);
        }
        tp.vertex_mapped = nullptr;
    }
    if (tp.vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, tp.vertex_buffer, nullptr);
        tp.vertex_buffer = VK_NULL_HANDLE;
    }
    if (tp.vertex_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, tp.vertex_memory, nullptr);
        tp.vertex_memory = VK_NULL_HANDLE;
    }
    if (tp.handle != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, tp.handle, nullptr);
        tp.handle = VK_NULL_HANDLE;
    }
    if (tp.layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, tp.layout, nullptr);
        tp.layout = VK_NULL_HANDLE;
    }
}
uint32_t append_text_run(TextPipeline& tp,
                         const AxylFont& font,
                         const char* utf8_text,
                         float origin_x_px,
                         float origin_y_px,
                         float pixel_size,
                         float r, float g, float b, float a) {
    if (!tp.vertex_mapped) return tp.glyph_count;

    TextVertex* verts = (TextVertex*)tp.vertex_mapped;
    uint32_t    glyph_index = tp.glyph_count;

    float cursor_x   = origin_x_px;
    float baseline_y = origin_y_px;
    float line_height = (font.ascender - font.descender) * pixel_size * 1.2f;
    //                                                                 ^^^ 20% leading

    float atlas_w = (float)font.atlas_width;
    float atlas_h = (float)font.atlas_height;

    for (const char* p = utf8_text; *p; p++) {
        uint32_t codepoint = (uint8_t)*p;

        // --- Newline handling: advance baseline, reset x, no glyph ---
        if (codepoint == '\n') {
            baseline_y += line_height;
            cursor_x    = origin_x_px;
            continue;
        }

        const Glyph* g_meta = font_get_glyph(font, codepoint);
        if (!g_meta) continue;

        bool has_quad = (g_meta->plane_right > g_meta->plane_left) &&
                        (g_meta->plane_top   > g_meta->plane_bottom);

        if (has_quad && glyph_index < tp.max_glyphs) {
            float x0 = cursor_x   + g_meta->plane_left   * pixel_size;
            float x1 = cursor_x   + g_meta->plane_right  * pixel_size;
            float y0 = baseline_y - g_meta->plane_top    * pixel_size;
            float y1 = baseline_y - g_meta->plane_bottom * pixel_size;

            float u0 = g_meta->atlas_left  / atlas_w;
            float u1 = g_meta->atlas_right / atlas_w;
            float v0 = 1.0f - g_meta->atlas_top    / atlas_h;
            float v1 = 1.0f - g_meta->atlas_bottom / atlas_h;

            TextVertex* v = &verts[glyph_index * 6];
            v[0].pos[0]=x0; v[0].pos[1]=y0; v[0].uv[0]=u0; v[0].uv[1]=v0;
            v[1].pos[0]=x1; v[1].pos[1]=y0; v[1].uv[0]=u1; v[1].uv[1]=v0;
            v[2].pos[0]=x0; v[2].pos[1]=y1; v[2].uv[0]=u0; v[2].uv[1]=v1;
            v[3].pos[0]=x1; v[3].pos[1]=y0; v[3].uv[0]=u1; v[3].uv[1]=v0;
            v[4].pos[0]=x1; v[4].pos[1]=y1; v[4].uv[0]=u1; v[4].uv[1]=v1;
            v[5].pos[0]=x0; v[5].pos[1]=y1; v[5].uv[0]=u0; v[5].uv[1]=v1;

            for (int i = 0; i < 6; i++) {
                v[i].color[0] = r; v[i].color[1] = g;
                v[i].color[2] = b; v[i].color[3] = a;
            }

            glyph_index++;
        }

        cursor_x += g_meta->advance * pixel_size;
    }

    tp.pen_x       = cursor_x;
    tp.baseline_y  = baseline_y;
    tp.pixel_size  = pixel_size;
    tp.glyph_count = glyph_index;
    return glyph_index;
}

uint32_t build_text_vertices (TextPipeline& tp,
                             const AxylFont& font,
                             const char* utf8_text,
                             float origin_x_px,
                             float origin_y_px,
                             float pixel_size,
                             float r, float g, float b, float a) {
    tp.glyph_count = 0;       // reset, then append at index 0
    return append_text_run(tp, font, utf8_text,
                           origin_x_px, origin_y_px, pixel_size,
                           r, g, b, a);
}


// In text.cpp — new function. Existing append_text_run unchanged.

uint32_t build_text_vertices_with_cursor(TextPipeline& tp,
                                          const AxylFont& font,
                                          const char* utf8_text,
                                          size_t cursor_byte_offset,
                                          float origin_x_px,
                                          float origin_y_px,
                                          float pixel_size,
                                          float r, float g, float b, float a) {
    tp.glyph_count = 0;
    if (!tp.vertex_mapped) return 0;

    TextVertex* verts = (TextVertex*)tp.vertex_mapped;
    uint32_t    glyph_index = 0;

    float cursor_x    = origin_x_px;
    float baseline_y  = origin_y_px;
    float line_height = (font.ascender - font.descender) * pixel_size * 1.2f;

    float atlas_w = (float)font.atlas_width;
    float atlas_h = (float)font.atlas_height;

    // Initialize cursor position to origin (in case the buffer is empty
    // or cursor is at offset 0).
    tp.cursor_pen_x      = cursor_x;
    tp.cursor_baseline_y = baseline_y;
    bool cursor_recorded = false;

    size_t byte_offset = 0;
    for (const char* p = utf8_text; *p; p++, byte_offset++) {
        // Snapshot pen state when we reach the cursor's byte offset.
        if (!cursor_recorded && byte_offset == cursor_byte_offset) {
            tp.cursor_pen_x      = cursor_x;
            tp.cursor_baseline_y = baseline_y;
            cursor_recorded = true;
        }

        uint32_t codepoint = (uint8_t)*p;

        if (codepoint == '\n') {
            baseline_y += line_height;
            cursor_x    = origin_x_px;
            continue;
        }

        const Glyph* g_meta = font_get_glyph(font, codepoint);
        if (!g_meta) continue;

        bool has_quad = (g_meta->plane_right > g_meta->plane_left) &&
                        (g_meta->plane_top   > g_meta->plane_bottom);

        if (has_quad && glyph_index < tp.max_glyphs) {
            float x0 = cursor_x   + g_meta->plane_left   * pixel_size;
            float x1 = cursor_x   + g_meta->plane_right  * pixel_size;
            float y0 = baseline_y - g_meta->plane_top    * pixel_size;
            float y1 = baseline_y - g_meta->plane_bottom * pixel_size;

            float u0 = g_meta->atlas_left  / atlas_w;
            float u1 = g_meta->atlas_right / atlas_w;
            float v0 = 1.0f - g_meta->atlas_top    / atlas_h;
            float v1 = 1.0f - g_meta->atlas_bottom / atlas_h;

            TextVertex* v = &verts[glyph_index * 6];
            v[0].pos[0]=x0; v[0].pos[1]=y0; v[0].uv[0]=u0; v[0].uv[1]=v0;
            v[1].pos[0]=x1; v[1].pos[1]=y0; v[1].uv[0]=u1; v[1].uv[1]=v0;
            v[2].pos[0]=x0; v[2].pos[1]=y1; v[2].uv[0]=u0; v[2].uv[1]=v1;
            v[3].pos[0]=x1; v[3].pos[1]=y0; v[3].uv[0]=u1; v[3].uv[1]=v0;
            v[4].pos[0]=x1; v[4].pos[1]=y1; v[4].uv[0]=u1; v[4].uv[1]=v1;
            v[5].pos[0]=x0; v[5].pos[1]=y1; v[5].uv[0]=u0; v[5].uv[1]=v1;

            for (int i = 0; i < 6; i++) {
                v[i].color[0] = r; v[i].color[1] = g;
                v[i].color[2] = b; v[i].color[3] = a;
            }

            glyph_index++;
        }

        cursor_x += g_meta->advance * pixel_size;
    }

    // Cursor at end of buffer.
    if (!cursor_recorded) {
        tp.cursor_pen_x      = cursor_x;
        tp.cursor_baseline_y = baseline_y;
    }

    tp.pen_x       = cursor_x;
    tp.baseline_y  = baseline_y;
    tp.pixel_size  = pixel_size;
    tp.glyph_count = glyph_index;
    return glyph_index;
}

bool append_cursor_quad(TextPipeline& tp,
                        const AxylFont& font,
                        float r, float g, float b, float a) {
    if (!tp.vertex_mapped || tp.glyph_count >= tp.max_glyphs) return false;

    const Glyph* solid = font_get_glyph(font, 'M');
    if (!solid) return false;

    float atlas_w = (float)font.atlas_width;
    float atlas_h = (float)font.atlas_height;

    float ucx = 0.5f * (solid->atlas_left + solid->atlas_right) / atlas_w;
    float vcy = 1.0f - 0.5f * (solid->atlas_top + solid->atlas_bottom) / atlas_h;

    float ps     = tp.pixel_size;
    float top    = tp.cursor_baseline_y - font.ascender  * ps;
    float bottom = tp.cursor_baseline_y - font.descender * ps;
    float x0     = tp.cursor_pen_x;
    float x1     = x0 + 2.0f;

    TextVertex* verts = (TextVertex*)tp.vertex_mapped;
    TextVertex* v     = &verts[tp.glyph_count * 6];

    auto set = [&](TextVertex& tv, float x, float y) {
        tv.pos[0] = x;   tv.pos[1] = y;
        tv.uv[0]  = ucx; tv.uv[1]  = vcy;
        tv.color[0] = r; tv.color[1] = g; tv.color[2] = b; tv.color[3] = a;
    };
    set(v[0], x0, top);    set(v[1], x1, top);    set(v[2], x0, bottom);
    set(v[3], x1, top);    set(v[4], x1, bottom); set(v[5], x0, bottom);

    tp.glyph_count++;
    return true;
}


uint32_t emit_selection_rects(SolidPipeline& sp, const AxylFont& font, const char* utf8_text, size_t sel_lo, size_t sel_hi, float origin_x_px, float origin_y_px, float pixel_size, float r, float g, float b, float a){
    if (sel_lo >= sel_hi) return 0;
    if (!utf8_text) return 0;

    float cursor_x = origin_x_px;
    float baseline = origin_y_px;

    float line_height = (font.ascender - font.descender) * pixel_size * 1.2f;
    float top_offset = font.ascender * pixel_size;
    float bot_offset = font.descender * pixel_size;

    bool in_selection = false;
    float rect_x_start = 0.0f;
    float rect_baseline = 0.0f;
    uint32_t rect_count = 0;
    
    auto flush_rect = [&](float x_end) {
        float top = rect_baseline - top_offset;
        float height = top_offset - bot_offset;
        if (x_end > rect_x_start){
            if(solid_push_rect (sp, rect_x_start, top, x_end-rect_x_start, height, r, g, b, a)) {
                rect_count++;
            }
        }
    };

    size_t byte_offset = 0;
    for (const char* p = utf8_text; *p; p++, byte_offset++){
        if(!in_selection && byte_offset == sel_lo){
            in_selection = true;
            rect_x_start = cursor_x;
            rect_baseline = baseline;
        }
        

        if (in_selection && byte_offset == sel_hi) {
            flush_rect(cursor_x);
            in_selection = false;
            return rect_count;
        }

        uint32_t codepoint = (uint8_t)*p;

        if(codepoint == '\n'){
            if(in_selection){
                float trail_end = cursor_x + 4.0f;
                flush_rect(trail_end);
                rect_baseline = baseline + line_height;
                rect_x_start = origin_x_px;
            }
            baseline += line_height;
            cursor_x = origin_x_px;
            continue;
        }

        const Glyph* g_meta = font_get_glyph(font, codepoint);
        if(!g_meta) continue;

        cursor_x += g_meta->advance*pixel_size;


    }

    if(in_selection){
        flush_rect(cursor_x);
    }

    return rect_count;

}