set(AXIOVOX_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "Use shared Microsoft runtime" FORCE)
endif()

get_filename_component(vulkan_library_directory "${Vulkan_LIBRARY}" DIRECTORY)
if(APPLE)
    find_file(AXIOVOX_MOLTENVK_LIBRARY NAMES libMoltenVK.dylib
        HINTS "$ENV{VULKAN_SDK}/lib" "${vulkan_library_directory}"
        REQUIRED
    )
    add_library(axiovox_vulkan_runtime SHARED IMPORTED)
    set_target_properties(axiovox_vulkan_runtime PROPERTIES IMPORTED_LOCATION "${AXIOVOX_MOLTENVK_LIBRARY}")
    target_compile_definitions(axiovox_vulkan_runtime INTERFACE AX_VULKAN_DIRECT_MOLTENVK)
    set(AXIOVOX_VULKAN_TARGET axiovox_vulkan_runtime)
    set(AXIOVOX_RUNTIME_SOURCE "${AXIOVOX_MOLTENVK_LIBRARY}")
    set(AXIOVOX_RUNTIME_DESTINATION "lib/libMoltenVK.dylib")
elseif(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(vulkan_binary_directory Bin)
        set(windows_runtime_directory "$ENV{SystemRoot}/System32")
    else()
        set(vulkan_binary_directory Bin32)
        set(windows_runtime_directory "$ENV{SystemRoot}/SysWOW64")
    endif()
    find_file(AXIOVOX_VULKAN_LOADER NAMES vulkan-1.dll
        HINTS "$ENV{VULKAN_SDK}/${vulkan_binary_directory}"
              "${vulkan_library_directory}/../${vulkan_binary_directory}"
              "${windows_runtime_directory}"
        REQUIRED
    )
    set(AXIOVOX_VULKAN_TARGET Vulkan::Vulkan)
    set(AXIOVOX_RUNTIME_SOURCE "${AXIOVOX_VULKAN_LOADER}")
    set(AXIOVOX_RUNTIME_DESTINATION "vulkan-1.dll")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(AXIOVOX_VULKAN_TARGET Vulkan::Vulkan)
    if(NOT Vulkan_LIBRARY MATCHES "\\.a$")
        set(AXIOVOX_RUNTIME_SOURCE "${Vulkan_LIBRARY}")
        set(AXIOVOX_RUNTIME_DESTINATION "lib/libvulkan.so.1")
    endif()
else()
    message(FATAL_ERROR "Distribution currently supports Windows, Linux, and macOS")
endif()

configure_file("${PROJECT_SOURCE_DIR}/cmake/StageDistribution.cmake.in"
               "${PROJECT_BINARY_DIR}/StageDistribution.cmake" @ONLY)
add_custom_target(axiovox_distribution
    COMMAND "${CMAKE_COMMAND}" -P "${PROJECT_BINARY_DIR}/StageDistribution.cmake"
    COMMENT "Staging bin assets, licenses, and Vulkan runtime"
    VERBATIM
)
