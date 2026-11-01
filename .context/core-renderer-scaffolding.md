# Core and renderer scaffolding

## Scope

Add core services, window management, Vulkan device selection events, and extensible
runtime command scaffolding. No commits/pushes. No swapchain/drawing or interactive
console transport in this foundation.

Current rendering/input/command behavior: see `dynamic-rendering-console.md`.
The following records the initial scaffold; command syntax has since changed.

## Initial boundaries

- Static `axiovox_core`: events, logger, config, command registry, jobs; no GPU dependencies.
- Vendored GLFW submodule builds statically with examples/tests/docs/install disabled.
  VMA upstream header target is linked to the application; upstream CMake detects C++.
  No allocator implementation yet. Vulkan SDK still supplied externally.
- Events/config/commands/window/renderer belong to main thread. Event payloads borrowed
  only during callbacks. Jobs use native Windows or POSIX threads and a bounded queue;
  submission failure is caller-visible. Shutdown drains and joins before GPU teardown.
- Config registry validates integer bounds. File load stages all changes before commit.
  Runtime window dimensions/log severity update via CONFIG_CHANGED observer.
- Device and worker changes mark restart pending; no implicit restart or device teardown.
- Renderer enumerates/logs all GPUs, checks swapchain extension, queues, formats/modes;
  ranks compatible GPUs and creates logical device before DEVICE_SELECTED emission.
  Explicit device index is enumeration-local, unsuitable selection fails clearly.
- Commands registered in main: set/get/config.save/quit. CLI --command uses the runtime
  executor after initialization; future UI/console can attach on main thread.

## Validation

macOS AppleClang build and core CTest passed; Vulkan smoke test created Apple M3 Pro
logical device and emitted selection event. Windows/Linux execution not available.
AddressSanitizer/UndefinedBehaviorSanitizer build, core CTest, and Vulkan smoke test
passed. Live window resize, restart-required device setting, quit, and invalid-command
failure/cleanup paths verified. `git diff --check` passed.
