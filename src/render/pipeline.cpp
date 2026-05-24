#include "pipeline.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include <unistd.h>     // readlink
#include <libgen.h>     // dirname
#include <climits>      // PATH_MAX

const char* exe_relative(const char* relpath) {
    static char buf[PATH_MAX *2 + 2];
    static char exe_dir[PATH_MAX];
    static bool initialized = false;

    if (!initialized) {
        ssize_t n = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
        if (n <= 0) {
            fprintf(stderr, "[fs] readlink /proc/self/exe failed: %s\n", strerror(errno));
            strcpy(exe_dir, ".");
        } else {
            exe_dir[n] = '\0';
            // dirname() may modify its argument and uses static storage; copy result.
            char tmp[PATH_MAX];
            strcpy(tmp, exe_dir);
            strcpy(exe_dir, dirname(tmp));
        }
        initialized = true;
    }

    snprintf(buf, sizeof(buf), "%s/%s", exe_dir, relpath);
    return buf;
}


//Load SPIR-V file into memory
static uint32_t* load_spirv(const char* path, size_t* size) {
    *size = 0;
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[shader] fopen failed for '%s': %s (CWD-relative)\n",
                path, strerror(errno));
        return nullptr;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "[shader] fseek end failed for '%s'\n", path);
        fclose(f);
        return nullptr;
    }
    long ftold = ftell(f);
    if (ftold <= 0) {
        fprintf(stderr, "[shader] '%s' is empty or ftell failed (got %ld)\n", path, ftold);
        fclose(f);
        return nullptr;
    }
    if ((ftold % 4) != 0) {
        fprintf(stderr, "[shader] '%s' size %ld is not a multiple of 4 — not valid SPIR-V\n",
                path, ftold);
        fclose(f);
        return nullptr;
    }
    fseek(f, 0, SEEK_SET);

    uint32_t* buffer = (uint32_t*)malloc((size_t)ftold);
    if (!buffer) {
        fprintf(stderr, "[shader] malloc(%ld) failed for '%s'\n", ftold, path);
        fclose(f);
        return nullptr;
    }

    size_t got = fread(buffer, 1, (size_t)ftold, f);
    fclose(f);
    if (got != (size_t)ftold) {
        fprintf(stderr, "[shader] short read for '%s': got %zu of %ld bytes\n",
                path, got, ftold);
        free(buffer);
        return nullptr;
    }

    // SPIR-V starts with magic 0x07230203 in little-endian
    if (buffer[0] != 0x07230203) {
        fprintf(stderr, "[shader] '%s' bad magic 0x%08x (expected 0x07230203). Not SPIR-V?\n",
                path, buffer[0]);
        free(buffer);
        return nullptr;
    }

    *size = (size_t)ftold;
    printf("[shader] loaded '%s' (%zu bytes)\n", path, *size);
    return buffer;
}

static VkShaderModule create_shader_module(VkDevice device, const uint32_t* code, size_t size){
    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = size;
    create_info.pCode = code;

    VkShaderModule module;
    vkCreateShaderModule(device, &create_info, nullptr, &module);
    return module;
}


Pipeline create_pipeline(VkDevice device, VkFormat color_format) {
    // Load shaders
    size_t vert_size, frag_size;
    uint32_t* vert_code = load_spirv(exe_relative("shaders/quad.vert.spv"), &vert_size);
    uint32_t* frag_code = load_spirv(exe_relative("shaders/quad.frag.spv"), &frag_size);

    if (!vert_code || !frag_code) {
        fprintf(stderr, "[pipeline] Failed to load shaders. CWD-relative path.\n");
        free(vert_code);
        free(frag_code);
        Pipeline empty{};
        return empty;
    }
    
    VkShaderModule vert_module = create_shader_module(device, vert_code, vert_size);
    VkShaderModule frag_module = create_shader_module(device, frag_code, frag_size);

    free(vert_code);
    free(frag_code);

    // Shader stages
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_module;
    stages[0].pName = "main";

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_module;
    stages[1].pName = "main";

    // Vertex input: position (vec2) + color (vec4)
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(float) * 6;  // 2 floats pos + 4 floats color
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributes[2]{};
    // Position at location 0
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[0].offset = 0;
    // Color at location 1
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[1].offset = sizeof(float) * 2;

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = 2;
    vertex_input.pVertexAttributeDescriptions = attributes;

    // Input assembly: draw triangles
    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Dynamic viewport and scissor (set at draw time, not baked into pipeline)
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamics;

    // Viewport state (count only, actual values are dynamic)
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    // Rasterizer: fill triangles, no culling (2D UI doesn't need it)
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    // Multisampling: off (no anti-aliasing for now)
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Color blending: alpha blending enabled (for text rendering later)
    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.blendEnable = VK_TRUE;
    blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                     VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT |
                                     VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend{};
    color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &blend_attachment;

    // Pipeline layout (empty for now — no push constants or descriptors yet)
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    Pipeline p{};
    vkCreatePipelineLayout(device, &layout_info, nullptr, &p.layout);

    // Dynamic rendering format (Vulkan 1.3 — no VkRenderPass needed)
    VkPipelineRenderingCreateInfo rendering_info{};
    rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachmentFormats = &color_format;

    // Create the pipeline
    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = &rendering_info;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &color_blend;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = p.layout;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                                 &pipeline_info, nullptr, &p.handle);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] Failed to create pipeline: %d\n", result);
    } else {
        printf("[vulkan] Pipeline created\n");
    }

    // Shader modules can be destroyed after pipeline creation
    vkDestroyShaderModule(device, vert_module, nullptr);
    vkDestroyShaderModule(device, frag_module, nullptr);

    return p;
}