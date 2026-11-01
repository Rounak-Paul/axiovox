# Dynamic renderer and console

## Scope / status

Implemented Vulkan dynamic rendering, ordered nodes with final UI, Causality font
atlas technique/embedded font, input and slash console. Dotted commands plus values;
argv dispatch for future Lua, no Lua linked. No commits or pushes.

Distribution updates: see `bin-distribution.md` (macOS now links MoltenVK directly).
The validation below describes the original loader-based renderer.

## Boundaries

- Independent CMake libraries: core, platform, renderer, console, font, UI. Core
  and console tests do not require a display or Vulkan device. GPU tests opt in
  with AXIOVOX_TEST_GPU. Vulkan SDK glslc compiles embedded shader includes.
- Loader/device negotiation supports Vulkan 1.3 core dynamic rendering or 1.2 +
  KHR extension. Feature checked/enabled before selection event. Existing device
  ranking and macOS portability path retained.
- Two frame fences/command buffers/acquire semaphores. Completion semaphores belong
  to swapchain images to avoid reuse before presentation consumes them. Recreation
  drains GPU; zero framebuffer skips work. Surface format changes fail cleanly
  rather than reuse incompatible node pipelines.
- Ordered node registration with enabled flag; reserved final UI cannot be bypassed
  by ordinary node order. Registry cannot mutate during recording. Initial background
  node actually clears color, so runtime disable is observable. Single color scope;
  no automatic resource graph, separate compute/transfer/depth passes yet.
- Input adapter in window callbacks feeds core key/text/focus events and pointer state.
  Navigation, alphabet, numbers, F1-F12, modifiers supported. Gameplay accessors honor
  UI capture. Unmodified physical slash opens the console; matching OS slash text is suppressed
  once. Text-only slash fallback supports other layouts. See slash-console-input.md.
- Console: bounded UTF-8 line editing/cursor scrolling, history/draft restoration,
  live sorted suggestions with keyboard selection (see console-suggestions.md),
  completion, output ring, Enter/Escape. Recent output stays visible while closed.
- Namespace commands: dotted settings accept zero values=get, one=set. Built-ins
  core.help/core.quit/core.config.save/core.config.load/renderer.nodes/
  renderer.node.enable. Text adapter handles quotes/backslash; direct argv invocation
  supports future scripting without a console/parser dependency. Device/workers
  remain restart-only desired values. All dispatch is main-thread.
- Font source inspected in /Users/duke/Code/causality/causality/src/renderer/font.c.
  Original TTF extracted from embedded_font.c. stb header copied with original license;
  original font metadata, Apache and Nerd Fonts licenses included. R8 atlas covers
  ASCII/Latin-1 at 32px, rendered at 16 logical px; other UTF-8 preserved, shown as ?.
  Generated font C is line-wrapped for compiler portability. No runtime font path.
- Memory helpers handle the small atlas/staging/vertex allocations directly; VMA
  remains header-only pending actual allocator integration. Fence-protected per-frame
  UI vertex buffers prevent host/GPU writes racing. Shutdown drains before UI free.

## Validation

- macOS Apple M3 Pro: Debug build, core/config/job tests, console/parser/input tests,
  real GPU node-order/enable/resize test, eight-frame UI/presentation smoke test passed.
- AddressSanitizer + UndefinedBehaviorSanitizer build passed the same four CTests.
- Vulkan Khronos validation confirmed loaded; tests also passed with synchronization
  validation enabled. No reported validation errors.
- One GPU screenshot inspected: legible font, command list, prompt/cursor placement.
- Windows/Linux execution and Vulkan 1.2-only fallback not independently exercised.

World renderer, camera, cursor lock, and node removal: see `voxel-world-renderer.md`.
