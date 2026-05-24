// axl_test.cpp
// Build: g++ -std=c++20 axl_test.cpp -o axl_test && ./axl_test
// Returns 0 on success, 1 on failure. CI/CD compatible.

#include "../src/core/types.h"
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

using namespace axel;

static int tests_run = 0;
static int tests_passed = 0;



#define TEST(name) \
    do { \
        tests_run++; \
        printf(" [TEST] %-40s ", name); \
    } while(0)

#define ASSERT(cond) \
    if (!(cond)) {\
        printf("FAIL (%s:%d)\n", __FILE__, __LINE__); \
        return false; \
    }

#define PASS() \
    tests_passed++; \
    printf("OK\n")


// ── Test: struct sizes ──
bool test_struct_sizes() {
    TEST("BlockHeader is 1 byte");
    ASSERT(sizeof(BlockHeader) == 1);
    PASS();

    TEST("Block is 12 bytes");
    ASSERT(sizeof(Block) == 12);
    PASS();

    TEST("Page is 22 bytes");
    ASSERT(sizeof(Page) == 22);
    PASS();

    TEST("Project is 10 bytes");
    ASSERT(sizeof(Project) == 10);
    PASS();

    TEST("AXLHeader is 33 bytes");
    ASSERT(sizeof(AXLHeader) == 33);
    PASS();

    return true;
}


// ── Test: tiered timestamp encoding ──
bool test_timestamps() {
    TEST("Tier 0: raw minutes (<11 days)");
    uint16_t t1 = encode_delta(500);
    ASSERT(decode_delta(t1) == 500);
    ASSERT((t1 >> 14) == 0);
    PASS();

    TEST("Tier 1: hours (~1.9 years)");
    uint16_t t2 = encode_delta(20000);  // ~13 days
    ASSERT((t2 >> 14) == 1);
    uint32_t decoded = decode_delta(t2);
    uint32_t error = decoded > 20000 ? decoded - 20000 : 20000 - decoded;
    ASSERT(error < 60);  // within 1 hour precision
    PASS();

    TEST("Tier 2: days (~44 years)");
    uint16_t t3 = encode_delta(1000000);  // ~694 days
    ASSERT((t3 >> 14) == 2);
    decoded = decode_delta(t3);
    error = decoded > 1000000 ? decoded - 1000000 : 1000000 - decoded;
    ASSERT(error < 1440);  // within 1 day precision
    PASS();

    TEST("Edge: zero minutes");
    ASSERT(decode_delta(encode_delta(0)) == 0);
    PASS();

    TEST("Edge: max tier 0 (16383 min)");
    uint16_t t4 = encode_delta(16383);
    ASSERT((t4 >> 14) == 0);
    ASSERT(decode_delta(t4) == 16383);
    PASS();

    TEST("Edge: tier 0 to tier 1 boundary");
    uint16_t t5 = encode_delta(16384);
    ASSERT((t5 >> 14) == 1);
    PASS();

    TEST("today_as_day returns reasonable value");
    uint16_t today = today_as_day();
    ASSERT(today > 20000);  // after ~2024
    ASSERT(today < 30000);  // before ~2052
    PASS();

    return true;
}

// ── Test: BlockHeader bit fields ──
bool test_block_header() {
    TEST("BlockHeader kind/subkind/flags pack correctly");
    BlockHeader h{};
    h.kind = 2;      // embed
    h.subkind = 5;   // should be 5
    h.flags = 7;     // all 3 flags set
    ASSERT(h.kind == 2);
    ASSERT(h.subkind == 5);
    ASSERT(h.flags == 7);
    PASS();

    TEST("BlockHeader kind max value (2)");
    BlockHeader h2{};
    h2.kind = 2;
    ASSERT(h2.kind == 2);
    PASS();

    TEST("BlockHeader subkind max value (7)");
    BlockHeader h3{};
    h3.subkind = 7;
    ASSERT(h3.subkind == 7);
    PASS();

    return true;
}

// ── Test: write and mmap read ──
bool test_write_read() {
    const char* filename = "/tmp/axl_test.axl";

    // Build string pool
    char string_pool[4096];
    uint32_t pool_pos = 0;

    auto add_string = [&](const char* str) -> uint32_t {
        uint32_t offset = pool_pos;
        uint32_t len = strlen(str);
        memcpy(string_pool + pool_pos, str, len);
        pool_pos += len;
        return offset;
    };

    // Create data
    uint32_t proj_name_off = add_string("Linear Algebra III");
    Project project{};
    project.name_offset = proj_name_off;
    project.id = {1};
    project.page_count = {1};
    project.name_length = 18;
    project.color = 3;

    uint32_t page_title_off = add_string("Eigenvalues");
    Page page{};
    page.title_offset = page_title_off;
    page.title_length = 11;
    page.block_start = 0;
    page.block_count = 3;
    page.id = {1};
    page.project_id = {1};
    page.created_at = encode_delta(0);
    page.updated_at = encode_delta(30);

    const char* texts[] = {
        "Eigenvalue Decomposition",
        "Av = lambda v where lambda is a scalar.",
        "Symmetric matrices have real eigenvalues.",
    };

    Block blocks[3];
    for (int i = 0; i < 3; i++) {
        blocks[i].content_offset = add_string(texts[i]);
        blocks[i].content_length = (uint16_t)strlen(texts[i]);
        blocks[i].page_id = {1};
        blocks[i].timestamp = encode_delta(i * 10);
        blocks[i].annotation_count = 0;
        blocks[i].header = {};
        blocks[i].header.kind = 0;
        blocks[i].header.subkind = (i == 0) ? 1 : 0;
    }

    // Build header
    AXLHeader header{};
    header.magic[0] = 'A'; header.magic[1] = 'X';
    header.magic[2] = 'L'; header.magic[3] = 'E';
    header.version = 1;
    header.flags = 0;
    header.project_count = 1;
    header.page_count = 1;
    header.block_count = 3;
    header.base_timestamp = today_as_day();
    header.string_pool_offset = sizeof(AXLHeader)
                                + sizeof(Project)
                                + sizeof(Page)
                                + 3 * sizeof(Block);
    header.string_pool_size = pool_pos;
    header.blob_pool_offset = header.string_pool_offset + pool_pos;
    header.blob_pool_size = 0;

    // Write
    FILE* f = fopen(filename, "wb");
    if (!f) { printf("FAIL (cannot create file)\n"); return false; }
    fwrite(&header, sizeof(AXLHeader), 1, f);
    fwrite(&project, sizeof(Project), 1, f);
    fwrite(&page, sizeof(Page), 1, f);
    fwrite(blocks, sizeof(Block), 3, f);
    fwrite(string_pool, 1, pool_pos, f);
    fclose(f);

    // Read back via mmap
    int fd = open(filename, O_RDONLY);
    struct stat st;
    fstat(fd, &st);
    void* mapped = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    auto* h = (const AXLHeader*)mapped;
    auto* p = (const Project*)((uint8_t*)mapped + sizeof(AXLHeader));
    auto* pg = (const Page*)((uint8_t*)mapped + sizeof(AXLHeader)
                              + h->project_count * sizeof(Project));
    auto* bl = (const Block*)((uint8_t*)mapped + sizeof(AXLHeader)
                              + h->project_count * sizeof(Project)
                              + h->page_count * sizeof(Page));
    auto* str = (const char*)((uint8_t*)mapped + h->string_pool_offset);

    // Verify header
    TEST("Magic bytes are AXLE");
    ASSERT(memcmp(h->magic, "AXLE", 4) == 0);
    PASS();

    TEST("Version is 1");
    ASSERT(h->version == 1);
    PASS();

    TEST("Counts match");
    ASSERT(h->project_count == 1);
    ASSERT(h->page_count == 1);
    ASSERT(h->block_count == 3);
    PASS();

    TEST("Project name reads correctly");
    ASSERT(memcmp(str + p[0].name_offset, "Linear Algebra III", 18) == 0);
    PASS();

    TEST("Project color is purple (3)");
    ASSERT(p[0].color == 3);
    PASS();

    TEST("Page title reads correctly");
    ASSERT(memcmp(str + pg[0].title_offset, "Eigenvalues", 11) == 0);
    PASS();

    TEST("Page block range correct");
    ASSERT(pg[0].block_start == 0);
    ASSERT(pg[0].block_count == 3);
    PASS();

    TEST("Block 0 is heading");
    ASSERT(bl[0].header.kind == 0);
    ASSERT(bl[0].header.subkind == 1);
    PASS();

    TEST("Block 1 is plain text");
    ASSERT(bl[1].header.kind == 0);
    ASSERT(bl[1].header.subkind == 0);
    PASS();

    TEST("Block content reads correctly");
    ASSERT(memcmp(str + bl[0].content_offset, "Eigenvalue Decomposition", 24) == 0);
    ASSERT(memcmp(str + bl[1].content_offset, "Av = lambda v", 13) == 0);
    ASSERT(memcmp(str + bl[2].content_offset, "Symmetric matrices", 18) == 0);
    PASS();

    TEST("Block timestamps are sequential");
    ASSERT(decode_delta(bl[0].timestamp) == 0);
    ASSERT(decode_delta(bl[1].timestamp) == 10);
    ASSERT(decode_delta(bl[2].timestamp) == 20);
    PASS();

    TEST("File size is minimal");
    ASSERT(st.st_size < 500);
    PASS();

    // Print actual size for human verification
    printf("\n  File size: %lld bytes\n", (long long)st.st_size);
    printf("  Structs:   %zu bytes\n",
           sizeof(AXLHeader) + sizeof(Project)
           + sizeof(Page) + 3 * sizeof(Block));
    printf("  Strings:   %u bytes\n", pool_pos);

    // Cleanup
    munmap(mapped, st.st_size);
    close(fd);
    unlink(filename);

    return true;
}

int main() {
    printf("\n╔═══════════════════════════════════╗\n");
    printf("║   AXLE Format Test Suite v0.1     ║\n");
    printf("╚═══════════════════════════════════╝\n\n");

    test_struct_sizes();
    test_timestamps();
    test_block_header();
    test_write_read();

    printf("\n──────────────────────────────────────\n");
    printf("  %d/%d tests passed\n", tests_passed, tests_run);
    printf("──────────────────────────────────────\n\n");

    return (tests_passed == tests_run) ? 0 : 1;
}