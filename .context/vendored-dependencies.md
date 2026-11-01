# Vendored dependencies

- User added `vendors/glfw` and `vendors/vma` as Git submodules. Preserve their
  staged gitlinks and `.gitmodules`; no commits/pushes or vendor source edits.
- Root CMake uses add_subdirectory for both, with a clear missing-submodule error.
  GLFW is static; its auxiliary builds/install and VMA install are disabled.
- Platform links glfw; renderer links GPUOpen::VulkanMemoryAllocator. System glfw3 package
  discovery removed. Vulkan headers/loader remain external SDK dependencies.
- VMA upstream enables C++; game sources stay C17. Its target provides headers
  only. Actual allocator integration requires exactly one C++14 implementation TU.
- README documents recursive submodule initialization and Linux backend dependencies.

Validation: fresh Debug configure/build in build-vendored passed on macOS;
GLFW compiled from vendored C/Objective-C sources. Core CTest and Vulkan device
smoke test passed (Apple M3 Pro). git diff --check passed. Windows/Linux unverified.

Dynamic renderer changes and font/stb provenance: see dynamic-rendering-console.md.
