#pragma once
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <vulkan/vulkan.h>
#include "renderer.h"
#include "pipeline.h"
#include "atlas.h"
#include "text.h"
#include "solid.h"

inline void print_resource_usage() {
    // RAM from /proc/self/status
    FILE* f = fopen("/proc/self/status", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "VmRSS:", 6) == 0) {
                printf("[resources] %s", line);  // resident memory
                break;
            }
        }
        fclose(f);
    }
    
    // CPU from /proc/self/stat
    f = fopen("/proc/self/stat", "r");
    if (f) {
        long utime, stime;
        // skip first 13 fields, read utime and stime
        fscanf(f, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %ld %ld", &utime, &stime);
        printf("[resources] CPU ticks: user=%ld kernel=%ld\n", utime, stime);
        fclose(f);
    }
}

struct Timer {
    std::chrono::high_resolution_clock::time_point start;
    
    Timer() : start(std::chrono::high_resolution_clock::now()) {}
    
    double elapsed_ms() {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(now - start).count();
    }
    
    void log(const char* label) {
        printf("[timer] %-40s %.3f ms\n", label, elapsed_ms());
        start = std::chrono::high_resolution_clock::now(); // reset for next measurement
    }
};

// Include these AFTER Timer, BEFORE VulkanState
#include "vulkan_init.h"
#include "swapchain.h"



struct VulkanState {
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    GPU gpu{};
    VulkanDevice vkdev{};
    Swapchain swapchain{};
    Renderer renderer{};
    Pipeline pipeline{};
    Atlas atlas{};
    std::atomic<bool> ready{false};
    std::atomic<bool> failed{false};
    bool swapchain_dirty = false;
    VkSwapchainKHR pending_destroy_swapchain = VK_NULL_HANDLE;
    VkImageView pending_destroy_views[8] = {};
    uint32_t pending_destroy_count = 0;
    TextPipeline text{};
    SolidPipeline solid{};
};


namespace axel {

    // --- Type-safe IDs (prevents mixing Up IDs accidently) ---
    struct BLOCKID {uint32_t v;};
    struct PAGEID {uint16_t v;};
    struct PROJECTID {uint16_t v;};

    struct BlockHeader {
        uint8_t kind: 2;  //0-2 (text, compute, embed) - 3 values, 2 bits
        uint8_t subkind: 3; //0-7 per kind - 8 variants each
        uint8_t flags: 3; // 3 boolean flags
    };
    // Total: 1 byte for everything
    static_assert(sizeof(BlockHeader) == 1, "BlockHeader must be 1 byte");


    #pragma pack(push, 1)
    struct Block {
        uint32_t content_offset;    // 4 bytes
        uint16_t content_length;    // 2 bytes
        PAGEID page_id;             // 2 bytes
        int16_t timestamp;          // 2 bytes : tiered delta from base
        uint8_t annotation_count;  // 1 byte
        BlockHeader header;     // 1 byte 
    };

    #pragma pack(pop)
    static_assert(sizeof(Block) == 12, "Block should be 12 bytes layout changed unexpectedly");

    // -- Page --
    #pragma pack(push, 1)
    struct Page
    {
        uint32_t title_offset;  // 4 bytes - into string pool
        uint16_t title_length;  // 2 bytes
        uint32_t block_start;   // 4 bytes - index into block table
        uint32_t block_count;   // 4 bytes
        PAGEID id;              // 2 bytes
        PROJECTID project_id;   // 2 bytes
        int16_t created_at;     // 2 bytes: tiered delta
        int16_t updated_at;     // 2 bytes: tiered dekta
    };
    // Total: 40 bytes
    #pragma pack(pop)
    static_assert(sizeof(Page) == 22, "Page is 22 bytes");

    // --- Project (was Notebook) ---
    #pragma pack(push, 1)
    struct Project
    {
        uint32_t name_offset;   // 4 bytes
        PROJECTID id;           // 2 bytes
        PAGEID page_count;      // 2 bytes
        uint8_t name_length;    // 1 byte
        uint8_t color;          // 1 byte
    };
    // Total 10 bytes
    #pragma pack(pop)
    static_assert(sizeof(Project) == 10, "Project is 10 bytes");

    constexpr uint32_t PROJECT_COLORS[] = {

        0xFF125A56,  //  0  dark blue
        0xFF00767B,  //  1  teal
        0xFF238F9D,  //  2  blue-teal
        0xFF42A7C6,  //  3  light blue
        0xFF60BCE9,  //  4  bright blue
        0xFF9DCCEF,  //  5  sky blue
        0xFFC6DBED,  //  6  pale blue
        0xFFDEE6E7,  //  7  very pale blue
        0xFFECEADA,  //  8  pale yellow
        0xFFF0E6B2,  //  9  cream
        0xFFF9D576,  // 10  yellow
        0xFFFFB954,  // 11  orange
        0xFFFD9A44,  // 12  dark orange
        0xFFF57634,  // 13  vermillion
        0xFFE94C1F,  // 14  reddish
        0xFFD11807,  // 15  dark red
    
    };

    constexpr uint8_t PROJECT_COLOR_COUNT = 16;
    
    // --- .axl file header ---
    #pragma pack(push, 1)
    struct AXLHeader {
        uint8_t magic[5];               // 4 byte: "AXEL"
        uint8_t version;                // 1 byte: format version
        uint8_t flags;                  // 1 byte: 8 flags
        uint16_t project_count;         // 2 bytes: MATCH ProjectID
        uint16_t page_count;            // 2 bytes: matches PAGEID
        uint32_t block_count;           // 4 bytes: matches Blockcount
        uint16_t base_timestamp;        // 2 bytes: days since epoch (covers until 2149)
        uint32_t string_pool_offset;    // 4 bytes: into file (4gb MAX)
        uint32_t string_pool_size;      // 4 bytes
        uint32_t blob_pool_offset;      // 4 bytes
        uint32_t blob_pool_size;        // 4 bytes
    };
    #pragma pack(pop)
    static_assert(sizeof(AXLHeader) == 33, "AXLHeader is 33 bytes");
    
    // --- Tiered timestamp encoding ---
    // Top 2 bits = precision, bottom 14 bits = value
    // 00 = minutes  (0-16383 min  = ~11 days)
    // 01 = hours    (0-16383 hrs  = ~1.9 years)
    // 10 = days     (0-16383 days = ~44 years)
    // 11 = reserved


    constexpr uint16_t encode_delta(uint32_t minutes_since_base){
        if(minutes_since_base < 16384)
            return (uint16_t)minutes_since_base;
        uint32_t hours = minutes_since_base / 60;
        if(hours < 16384) return (0b01 << 14) | (uint16_t)hours;
        uint32_t days = minutes_since_base/1440;
        if (days < 16384) return (0b10 << 14) | (uint16_t)days;
        
        return(0b10 << 14) | 16383;
    }
    

    constexpr uint32_t decode_delta(uint16_t encoded){
        uint8_t tier = (uint8_t)(encoded >> 14);
        uint16_t value = encoded & 0x3FFF;

        switch (tier)
        {
        case 0: return value;
        case 1: return value * 60;
        case 2: return value * 1440;        
        default: return value * 1440;
        }
    }

    constexpr int64_t base_to_unix(uint16_t base_day){
        return (uint16_t)base_day * 86400;
    }

    inline uint16_t today_as_day(){
        return(uint16_t)(time(nullptr) / 86400);
    }

    constexpr int64_t reconstruct_time(uint16_t base_day, uint16_t delta){
        return base_to_unix(base_day) + (int64_t)decode_delta(delta)*60;
    }
    
}