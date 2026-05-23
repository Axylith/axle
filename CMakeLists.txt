# CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(axylith VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Find dependencies ────────────────────────────────
find_package(Vulkan REQUIRED)
find_package(X11 REQUIRED)

# ─── Main binary ──────────────────────────────────────
add_executable(axylith
    src/main.cpp
    src/window.cpp
    src/vulkan_init.cpp
    src/swapchain.cpp
    src/monitor.h
    src/renderer.cpp
    src/pipeline.cpp
    src/font.cpp
    src/atlas.cpp
    src/text.cpp
    src/editor.cpp
    src/solid.cpp
    src/command.cpp


)

# After add_executable
# ─── Shaders ──────────────────────────────────────────
find_program(GLSLC glslc)
if(GLSLC)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/shaders/quad.vert.spv
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
        COMMAND ${GLSLC} ${CMAKE_SOURCE_DIR}/shaders/quad.vert
                -o ${CMAKE_BINARY_DIR}/shaders/quad.vert.spv
        DEPENDS ${CMAKE_SOURCE_DIR}/shaders/quad.vert
    )
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/shaders/quad.frag.spv
        COMMAND ${GLSLC} ${CMAKE_SOURCE_DIR}/shaders/quad.frag
                -o ${CMAKE_BINARY_DIR}/shaders/quad.frag.spv
        DEPENDS ${CMAKE_SOURCE_DIR}/shaders/quad.frag
    )
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/shaders/text.vert.spv
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders
        COMMAND ${GLSLC} ${CMAKE_SOURCE_DIR}/shaders/text.vert
                -o ${CMAKE_BINARY_DIR}/shaders/text.vert.spv
        DEPENDS ${CMAKE_SOURCE_DIR}/shaders/text.vert
    )
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/shaders/text.frag.spv
        COMMAND ${GLSLC} ${CMAKE_SOURCE_DIR}/shaders/text.frag
                -o ${CMAKE_BINARY_DIR}/shaders/text.frag.spv
        DEPENDS ${CMAKE_SOURCE_DIR}/shaders/text.frag
    )


    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/shaders/solid.frag.spv
        COMMAND ${GLSLC} ${CMAKE_SOURCE_DIR}/shaders/solid.frag
                -o ${CMAKE_BINARY_DIR}/shaders/solid.frag.spv
        DEPENDS ${CMAKE_SOURCE_DIR}/shaders/solid.frag
    )

    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/shaders/solid.vert.spv
        COMMAND ${GLSLC} ${CMAKE_SOURCE_DIR}/shaders/solid.vert
                -o ${CMAKE_BINARY_DIR}/shaders/solid.vert.spv
        DEPENDS ${CMAKE_SOURCE_DIR}/shaders/solid.vert
    )

    add_custom_target(shaders ALL DEPENDS
        ${CMAKE_BINARY_DIR}/shaders/quad.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/quad.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/text.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/text.frag.spv
        ${CMAKE_BINARY_DIR}/shaders/solid.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/solid.frag.spv
    )
    add_custom_command(TARGET axylith POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/assets
                $<TARGET_FILE_DIR:axylith>/assets
        COMMENT "Copying assets to build directory"
    )
    add_dependencies(axylith shaders)
endif()

target_include_directories(axylith PRIVATE src)

target_link_libraries(axylith PRIVATE
    Vulkan::Vulkan
    X11::X11
    ${X11_Xext_LIB}
)

# ─── Warnings (strict but not -Werror yet) ────────────
target_compile_options(axylith PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion
)

# ─── Release optimizations ────────────────────────────
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(axylith PRIVATE -Os -ffunction-sections -fdata-sections -fno-rtti -fno-exceptions)
    target_link_options(axylith PRIVATE -s -Wl,--gc-sections)
endif()

# ─── Tests ────────────────────────────────────────────
add_executable(axl_test tests/axl_test.cpp)
target_include_directories(axl_test PRIVATE src)    # ← add this line
target_compile_options(axl_test PRIVATE -Wall -Wextra)

enable_testing()
add_test(NAME axl_test COMMAND axl_test)