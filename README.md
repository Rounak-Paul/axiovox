# axiovox

A C17/Vulkan game foundation for Windows, Linux, and macOS.

Requires CMake 3.24+, a C17 compiler with atomics, a C++ compiler (for VMA's upstream
CMake), and the Vulkan SDK including `glslc`. Runtime requires Vulkan 1.2 with
`VK_KHR_dynamic_rendering`, or Vulkan 1.3. GLFW and VMA are vendored submodules;
no system GLFW is used. VMA currently supplies headers only; allocator integration
will require one C++14 implementation translation unit. Game sources remain C.

On macOS, use the SDK with MoltenVK. Set `VULKAN_SDK` or `CMAKE_PREFIX_PATH` if
needed. Linux needs platform development dependencies for GLFW's enabled backends;
`-DGLFW_BUILD_X11=OFF` or `-DGLFW_BUILD_WAYLAND=OFF` disables an unused backend.

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
./bin/axiovox
```

All generators, including Visual Studio, put the game directly in project-root
`bin/` (`bin/axiovox.exe` on Windows). Tests, libraries, symbols, and intermediate
files stay in the build tree. The selected configuration overwrites the same game
output, so build Release last before publishing and do not build different trees
into `bin/` concurrently. Shaders and the font remain embedded; source assets are
also staged locally. `--smoke-test` renders eight frames with the console open.
Set `-DAXIOVOX_TEST_GPU=ON` to include display/GPU tests in CTest.

## Shipping

Publish the contents of `bin/` for the platform/architecture you built:

```text
bin/
  axiovox                  # axiovox.exe on Windows
  assets/
  licenses/
  lib/                     # MoltenVK on macOS; Vulkan loader on Linux
  vulkan-1.dll             # Windows only
```

Assets and licenses are refreshed on every game build, even if no source relinks;
removed source assets are removed from the staged copy. Edit source `assets/`, not
its generated `bin/assets/` copy. Font licenses are included under `assets/fonts/`.
The executable loads bundled libraries relative to itself, independently of the
launch directory. The Vulkan SDK and shader compiler are build-time dependencies.

macOS directly uses bundled MoltenVK, without a Vulkan loader or SDK ICD manifest.
Windows includes the Vulkan loader DLL and MSVC builds use the static C runtime.
Linux includes the Vulkan loader shared library. Target machines still need their
normal OS/desktop libraries and, on Windows/Linux, a compatible installed GPU driver.
Build Linux releases against the oldest distribution you intend to support.
Custom SDK layouts can set `AXIOVOX_MOLTENVK_LIBRARY` (macOS) or
`AXIOVOX_VULKAN_LOADER` (Windows) to the matching runtime library at configure time.
`bin/` is generated and ignored by Git.

## Console and runtime commands

Press `/` to open the console, Enter to execute, Escape to close. Arrow keys,
Home/End, Backspace/Delete edit the line; Up/Down recall history; Tab completes a
unique name or lists candidates. Console capture suppresses gameplay input queries.
Recent command output remains visible when the console closes.

Commands use `namespace.name [values...]`. Names have nonempty identifier segments
separated by dots. Quotes and backslash escapes support spaces in values; there is
no shell expansion. Commands are bounded to 16 arguments and 1023 bytes.

```text
/core.help
/window.width 1600
/window.width
/log.level 0
/renderer.nodes
/renderer.node.enable background false
/renderer.device 0
/core.config.save "my game.cfg"
/core.config.load "my game.cfg"
/core.quit
```

Settings: `window.width`, `window.height`, `renderer.device`, `jobs.workers`,
`log.level` (0 debug through 3 error). A setting with no value reads its current
value; one value writes it after validation. Width/height/log level apply live.
Device and worker count changes mark restart pending and take effect on next launch.
Device `-1` auto-selects; other indices refer to the current GPU enumeration and
are not stable hardware identifiers. Unavailable explicit selections fail startup.

Config files contain `key=value` lines, blank lines, or `#` comments; no whitespace
around keys. Loading validates the entire file before changing settings.

```sh
./bin/axiovox --config game.cfg --command "window.width 1600"
```

Config files load before initialization. Repeatable `--command` arguments execute
after initialization using the same dispatch as the interactive console.

## Architecture

- `core/`: synchronous typed events, logger, config, command registry, jobs, input,
  and GLFW window management. Input separates physical keys and Unicode text;
  focus loss releases held keys/buttons. Mouse position, button state, and scroll
  are available. Raw state is available to UI; gameplay queries honor capture.
- `renderer/`: compatible GPU enumeration/ranking, device/surface/swapchain,
  two frames in flight, explicit image transitions, resize/out-of-date recreation,
  and dynamic rendering without render passes or framebuffers. Discrete GPUs rank
  before integrated, virtual, then others; ties follow enumeration order.
- Render nodes record in registration order inside a single color rendering scope;
  background is the initial scene node, UI is always last. Scene nodes can be enabled
  at runtime. Node callbacks borrow frame context and must not change the registry,
  end rendering, or perform transfers/compute requiring another rendering scope.
  This is an ordered node pipeline, not an automatic resource/dependency graph.
- `ui/`: console state/editor and a separate Vulkan UI node. The R8 font atlas uses
  the Causality stb_truetype approach and its embedded Roboto Mono Nerd Font, with
  licenses retained under `assets/fonts/`. Atlas glyphs cover ASCII/Latin-1; other
  Unicode codepoints are preserved in command text and displayed as `?`. Font size
  is 16 logical pixels, baked at 32 physical pixels for 2x display scaling.
- `ax_command_invoke` accepts argv directly, independently of parsing or the UI.
  Future Lua bindings can use this dispatch and config validation. No Lua is linked.

Events, config, commands, input, window, renderer, and console belong to the main
thread. Event payloads are borrowed only during callbacks; recursion is capped at
32 levels. Subscriber owners and the event bus must remain valid during dispatch.
The logger serializes thread-safe stderr output. The worker pool uses native
Windows/POSIX threads, a 256-job bounded queue, and explicit submission backpressure.
Job data remains caller-owned until completion. Workers must return normally and
must not wait/destroy their own pool or access main-thread services directly.
Shutdown drains workers, waits for GPU work, and destroys UI, renderer, then window.
