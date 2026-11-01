cmake_minimum_required(VERSION 3.24)

project(axiovox VERSION 0.1.0 LANGUAGES C CXX)

find_package(Vulkan REQUIRED COMPONENTS glslc)
find_package(Threads REQUIRED)
include(cmake/Distribution.cmake)

foreach(dependency IN ITEMS glfw vma)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendors/${dependency}/CMakeLists.txt")
        message(FATAL_ERROR "Missing vendors/${dependency}. Run: git submodule update --init --recursive")
    endif()
endforeach()

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build GLFW examples" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "Build GLFW tests" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "Build GLFW docs" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "Install GLFW" FORCE)
set(GLFW_LIBRARY_TYPE STATIC CACHE STRING "GLFW library type" FORCE)
add_subdirectory(vendors/glfw EXCLUDE_FROM_ALL)

set(VMA_ENABLE_INSTALL OFF CACHE BOOL "Install VMA" FORCE)
add_subdirectory(vendors/vma EXCLUDE_FROM_ALL)

add_library(axiovox_core STATIC
    src/core/log.c
    src/core/event.c
    src/core/config.c
    src/core/files.c
    src/core/command.c
    src/core/input.c
    src/core/jobs.c
)
target_include_directories(axiovox_core PUBLIC src)
target_link_libraries(axiovox_core PUBLIC Threads::Threads)

set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${generated_dir}")
foreach(shader IN ITEMS ui.vert ui.frag world.vert world.frag world_sky.vert world_sky.frag world_composite.frag)
    add_custom_command(
        OUTPUT "${generated_dir}/${shader}.inc"
        COMMAND Vulkan::glslc --target-env=vulkan1.2 -O -mfmt=c
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}"
                -o "${generated_dir}/${shader}.inc"
        DEPENDS "shaders/${shader}" shaders/world_common.glsl
        VERBATIM
    )
endforeach()
add_custom_command(
    OUTPUT "${generated_dir}/world_shadow.frag.inc"
    COMMAND Vulkan::glslc --target-env=vulkan1.2 -O -mfmt=c -DAX_WORLD_SHADOW=1
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/world.frag"
            -o "${generated_dir}/world_shadow.frag.inc"
    DEPENDS "shaders/world.frag" shaders/world_common.glsl
    VERBATIM
)
add_custom_command(
    OUTPUT "${generated_dir}/embedded_font.c"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/DepartureMonoNerdFontMono-Regular.otf"
            "-DOUTPUT=${generated_dir}/embedded_font.c"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFont.cmake"
    DEPENDS assets/fonts/DepartureMonoNerdFontMono-Regular.otf cmake/EmbedFont.cmake
    VERBATIM
)
add_library(axiovox_font STATIC src/ui/font.c "${generated_dir}/embedded_font.c")
target_include_directories(axiovox_font SYSTEM PRIVATE vendors/stb)
set_target_properties(axiovox_font PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
if(UNIX)
    target_link_libraries(axiovox_font PUBLIC m)
endif()

add_library(axiovox_console STATIC src/ui/console.c)
target_link_libraries(axiovox_console PUBLIC axiovox_core)

add_library(axiovox_platform STATIC src/core/window.c)
target_link_libraries(axiovox_platform PUBLIC axiovox_core Vulkan::Headers ${AXIOVOX_VULKAN_TARGET} glfw)

add_library(axiovox_renderer STATIC src/renderer/renderer.c src/renderer/frame.c src/renderer/memory.c)
target_link_libraries(axiovox_renderer PUBLIC axiovox_platform)

add_library(axiovox_ui_layout STATIC src/ui/draw.c src/ui/hud.c src/ui/menu.c)
target_link_libraries(axiovox_ui_layout PUBLIC axiovox_console axiovox_font)

add_library(axiovox_ui STATIC src/ui/ui.c "${generated_dir}/ui.vert.inc" "${generated_dir}/ui.frag.inc")
target_include_directories(axiovox_ui PRIVATE "${generated_dir}")
target_link_libraries(axiovox_ui PUBLIC axiovox_renderer axiovox_ui_layout)

add_library(axiovox_meshoptimizer STATIC
    vendors/meshoptimizer/src/allocator.cpp
    vendors/meshoptimizer/src/indexgenerator.cpp
    vendors/meshoptimizer/src/simplifier.cpp
    vendors/meshoptimizer/src/vcacheoptimizer.cpp
    vendors/meshoptimizer/src/vfetchoptimizer.cpp)
set_target_properties(axiovox_meshoptimizer PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
target_include_directories(axiovox_meshoptimizer SYSTEM PUBLIC vendors/meshoptimizer/src)

add_library(axiovox_world STATIC src/math/noise.c src/world/terrain.c src/world/gravity.c src/world/mesh_optimizer.cpp src/world/camera.c src/world/surface.c src/world/coordinates.c src/world/scene.c src/world/geometry.c src/world/stream.c)
target_link_libraries(axiovox_world PUBLIC axiovox_core PRIVATE axiovox_meshoptimizer)
set_target_properties(axiovox_world PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
if(MSVC)
    target_compile_options(axiovox_world PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
endif()
target_include_directories(axiovox_world SYSTEM PRIVATE vendors)
if(UNIX)
    target_link_libraries(axiovox_world PUBLIC m)
endif()

add_library(axiovox_world_renderer STATIC src/world/world_renderer.c
    "${generated_dir}/world.vert.inc" "${generated_dir}/world.frag.inc" "${generated_dir}/world_shadow.frag.inc"
    "${generated_dir}/world_sky.vert.inc" "${generated_dir}/world_sky.frag.inc" "${generated_dir}/world_composite.frag.inc")
target_include_directories(axiovox_world_renderer PRIVATE "${generated_dir}")
target_link_libraries(axiovox_world_renderer PUBLIC axiovox_renderer axiovox_world)

add_library(axiovox_runtime STATIC src/app/runtime.c)
target_link_libraries(axiovox_runtime PUBLIC axiovox_renderer)

add_executable(axiovox src/main.c)
set_target_properties(axiovox PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<1:${AXIOVOX_OUTPUT_DIRECTORY}>"
    PDB_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/symbols"
)
if(APPLE)
    set_target_properties(axiovox PROPERTIES BUILD_WITH_INSTALL_RPATH YES INSTALL_RPATH "@executable_path/lib")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(axiovox PROPERTIES BUILD_WITH_INSTALL_RPATH YES INSTALL_RPATH "$ORIGIN/lib")
elseif(MINGW)
    target_link_options(axiovox PRIVATE -static-libgcc)
endif()
add_dependencies(axiovox axiovox_distribution)

set_target_properties(axiovox axiovox_core axiovox_console axiovox_platform axiovox_renderer axiovox_ui axiovox_ui_layout axiovox_runtime axiovox_world axiovox_world_renderer PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

target_link_libraries(axiovox_renderer PRIVATE GPUOpen::VulkanMemoryAllocator)
target_link_libraries(axiovox PRIVATE axiovox_ui axiovox_runtime axiovox_world_renderer)

foreach(target IN ITEMS axiovox axiovox_core axiovox_console axiovox_platform axiovox_renderer axiovox_ui axiovox_ui_layout axiovox_font axiovox_runtime axiovox_world axiovox_world_renderer)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    elseif(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

include(CTest)
if(BUILD_TESTING)
    add_executable(axiovox_core_tests tests/core_tests.c)
    target_link_libraries(axiovox_core_tests PRIVATE axiovox_core)
    set_target_properties(axiovox_core_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    add_test(NAME core COMMAND axiovox_core_tests)
    set_tests_properties(core PROPERTIES TIMEOUT 30)
    add_executable(axiovox_console_tests tests/console_tests.c)
    target_link_libraries(axiovox_console_tests PRIVATE axiovox_console)
    set_target_properties(axiovox_console_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    add_test(NAME console COMMAND axiovox_console_tests)
    set_tests_properties(console PROPERTIES TIMEOUT 30)

    add_executable(axiovox_ui_tests tests/ui_tests.c)
    target_link_libraries(axiovox_ui_tests PRIVATE axiovox_ui_layout)
    set_target_properties(axiovox_ui_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    add_test(NAME ui COMMAND axiovox_ui_tests)
    set_tests_properties(ui PROPERTIES TIMEOUT 30)

    add_executable(axiovox_menu_tests tests/menu_tests.c)
    target_link_libraries(axiovox_menu_tests PRIVATE axiovox_ui_layout)
    set_target_properties(axiovox_menu_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    add_test(NAME menu COMMAND axiovox_menu_tests)
    set_tests_properties(menu PROPERTIES TIMEOUT 30)

    add_executable(axiovox_procedural_tests tests/procedural_tests.c)
    target_link_libraries(axiovox_procedural_tests PRIVATE axiovox_world)
    set_target_properties(axiovox_procedural_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    add_test(NAME procedural COMMAND axiovox_procedural_tests)
    set_tests_properties(procedural PROPERTIES TIMEOUT 60)

    add_executable(axiovox_surface_tests tests/surface_tests.c)
    target_link_libraries(axiovox_surface_tests PRIVATE axiovox_world)
    set_target_properties(axiovox_surface_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    add_test(NAME surface COMMAND axiovox_surface_tests)
    set_tests_properties(surface PROPERTIES TIMEOUT 60)

    add_executable(axiovox_stream_tests tests/stream_tests.c)
    target_link_libraries(axiovox_stream_tests PRIVATE axiovox_world)
    set_target_properties(axiovox_stream_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    add_test(NAME stream COMMAND axiovox_stream_tests)
    set_tests_properties(stream PROPERTIES TIMEOUT 180)

    add_executable(axiovox_scene_tests tests/scene_tests.c)
    target_link_libraries(axiovox_scene_tests PRIVATE axiovox_world)
    set_target_properties(axiovox_scene_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    add_test(NAME scene COMMAND axiovox_scene_tests)
    set_tests_properties(scene PROPERTIES TIMEOUT 60)

    add_executable(axiovox_world_tests tests/world_tests.c)
    target_link_libraries(axiovox_world_tests PRIVATE axiovox_world)
    set_target_properties(axiovox_world_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    add_test(NAME world COMMAND axiovox_world_tests)
    set_tests_properties(world PROPERTIES TIMEOUT 60)

    add_executable(axiovox_renderer_tests tests/renderer_tests.c)
    target_link_libraries(axiovox_renderer_tests PRIVATE axiovox_renderer)
    set_target_properties(axiovox_renderer_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    add_executable(axiovox_runtime_tests tests/runtime_tests.c)
    target_link_libraries(axiovox_runtime_tests PRIVATE axiovox_ui axiovox_runtime)
    set_target_properties(axiovox_runtime_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    add_executable(axiovox_world_render_tests tests/world_render_tests.c)
    target_link_libraries(axiovox_world_render_tests PRIVATE axiovox_world_renderer)
    set_target_properties(axiovox_world_render_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    option(AXIOVOX_TEST_GPU "Enable tests requiring a display and Vulkan GPU" OFF)
    if(AXIOVOX_TEST_GPU)
        add_test(NAME renderer COMMAND axiovox_renderer_tests)
        add_test(NAME presentation COMMAND axiovox --smoke-test)
        add_test(NAME runtime COMMAND axiovox_runtime_tests)
        add_test(NAME world_render COMMAND axiovox_world_render_tests)
        add_test(NAME live_presentation COMMAND axiovox --smoke-test --command "jobs.workers 2" --command "renderer.device 0" --command "renderer.device -1")
        set_tests_properties(renderer presentation runtime world_render live_presentation PROPERTIES TIMEOUT 30)
    endif()
endif()
