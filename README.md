# axiovox

A C17/Vulkan game foundation for Windows, Linux, and macOS.

Requires CMake 3.24+, a C17 compiler with atomics, a C++17 compiler (for the meshoptimizer integration and VMA's upstream
CMake), and the Vulkan SDK including `glslc`. Runtime requires Vulkan 1.2 with
`VK_KHR_dynamic_rendering`, or Vulkan 1.3. GLFW and VMA are vendored submodules;
no system GLFW is used. VMA currently supplies headers only; allocator integration
will require one C++14 implementation translation unit. Game systems remain C17; the meshoptimizer integration is a narrow, exception-safe C++17 boundary with a C API.

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
also staged locally. `--smoke-test` renders eight frames across the console, pause menu, and settings screens.
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

## World and camera

The world uses **solid procedural density volumes with rasterized triangle surfaces**.
Terra (radius 1,000 km) and Luna (radius 270 km) share one continuous scene. Indexed
surfaces are extracted with Transvoxel-compatible marching cubes, and transition
cells stitch neighboring 2:1 detail levels. There is no separate globe/space scene.
Density and material queries remain authoritative; triangle meshes are disposable
render assets. The former octree ray caster, beam passes and temporary demo are removed.

Moving the camera streams terrain automatically. Workers sample immutable scene
snapshots, reuse unchanged surface meshes, and publish complete nested LOD layouts
so neighboring levels stay consistent. Old coverage remains until replacements are
ready. Independent changed regions build in parallel; serial and parallel output are regression-tested for identical results. Finer regions own their volume, including excavations. The default finest
region spans 32 m with 64 cells per side (0.5 m spacing); distant levels use coarser
sampling. Resolution is independent of the precise coordinate addressing system.

Terrain composes seeded 3D gradient/value noise, normalized fBm, ridged noise,
cellular fields and domain warping. Terra combines continental basins, plains, hills,
mountain provinces, ridges, detail and bounded cave channels. Luna uses layered crater
bowls/rims and rough highlands. Spherical sampling has no latitude/longitude seams.
Temperature, moisture, elevation and slope classify grass, soil, rock, sand and snow.
These are procedural landforms, not simulated erosion, drainage or ocean fluids.

Generated meshes remove triangles covered by finer regions, then use meshoptimizer
for attribute-aware simplification and vertex-cache/fetch ordering. A two-cell band
around active LOD boundaries and material seams remains protected. Simplification
targets 15% of visible source triangles, constrained by topology and a 0.15-cell
absolute error metric; complex terrain may retain more triangles. Moving an inner
boundary invalidates its parent mesh so cached simplification cannot open cracks.

Rendering uses indexed triangles, backface culling, infinite reverse-Z depth,
directional shadow mapping, an atmosphere/space background, and an offscreen world
pass composited before UI. GPU transforms are camera-relative; integer addresses
and bounded double offsets retain local movement precision at planet scale.

Free-flight controls: mouse looks, W/S move along the view direction, A/D strafe,
and E/Q move along local gravity-up. Base speed is 6 m/s. Hold **Shift** for cruise:
speed increases with terrain clearance, allowing surface-to-space travel. The cursor is locked
and hidden during gameplay; menus, console, and focus loss release it.

Gravity sums all bodies using configured surface acceleration: inverse-square outside,
finite uniform-density acceleration inside. The free-flight camera parallel-transports
its tangent frame toward net gravity-up. Alignment fades smoothly between 0.25 and
0.02 m/s²; below that it preserves its space orientation. Gravity does not pull the
camera down or introduce a walking/collision mode. Terra uses 9.81 m/s² and Luna
1.62 m/s². Visits start at a terrain-aware temperate site with a properly oriented camera.

```text
/world.planets
/world.visit 2
/world.carve 3 12
/world.build 1 8 box
/world.speed 100
/world.altitude 2000000
/world.save "my-scene.axs"
/world.load "my-scene.axs"
```

`world.visit` is an optional navigation shortcut; flight between bodies requires no
scene transition. `world.altitude` moves radially above the reference planet's unedited terrain for
inspection; look toward the body to see its sphere. `world.speed` sets base speed in
meters per second. `world.recenter` and `world.refresh` remain compatible commands,
but recentering happens automatically.

Carve/build take radius (box half extent) and distance ahead in meters; optional shape
is `sphere` (default) or `box`. Edits affect the nearest body. Saves store planet
recipes, material settings, and ordered edits independently of render caches.
Version 3 saves include terrain recipe, cave depth and surface gravity, and load versions 1/2 with their original wave geometry. Camera pose is not saved.

Terrain generation is asynchronous, but publishing new vertex/index buffers still waits
for GPU completion. Stitching closes spatial LOD seams; temporal LOD changes can
still pop. Spatial edit indexing and upload scheduling remain optimization work. The moon's position is static, and character
gravity/collision and orbital simulation are not implemented.

## Game HUD

The voxel-world HUD has a center crosshair, a compact nine-slot hotbar, and two
icon-only bottom-left buttons: `/` opens commands and pause toggles the pause state.
Escape also toggles pause outside the console. Pause emits `AX_EVENT_PAUSE_CHANGED`
for future simulation systems; rendering continues. No keyboard-help captions or
wordmark are drawn.

Hotbar rows are empty scaffolding until inventory is added. 1–9 or the mouse wheel
selects a slot. Tab cycles rows; Shift+Tab cycles backwards. Each row remembers its
selection. Console and pause capture prevent hotbar actions, so Tab retains console
completion behavior while typing. Small markers indicate the active hotbar row.

`ui.hotbar.rows` sets 1–9 rows (default 3). `ui.scale` applies globally to text,
panels, icons, hit targets, crosshair, and hotbar: levels 1–5 correspond to 0.75x,
1x, 1.25x, 1.5x, and 2x. Default is level 2. Both settings apply live and are saved
with the other configuration values. The hotbar has no separate size setting.

```text
/ui.scale 3
/ui.hotbar.rows 4
```

## Pause and settings

The pause icon or Escape opens a centered **AXIOVOX** pause menu with Resume,
Settings, and Quit game. Gameplay input is captured while menus are open. Mouse
buttons activate on release inside the pressed control; keyboard navigation also
works, without displaying generic key-help captions. Escape returns from Settings
to Pause, then resumes from Pause. Closing a console over a paused game keeps it
paused.

Settings are grouped into Interface (UI scale/hotbar rows), Video (window resolution
and detected compatible graphics devices), and System (worker threads/log verbosity).
Edits are staged: **Apply** saves and applies them, **Done** saves and returns, and
**Cancel** discards pending edits. **Reset all** restores defaults in the draft.
All current settings apply live. GPU changes rebuild the logical device and
node-owned graphics resources while preserving console, menu, and HUD state.
Worker changes resize the existing pool without losing accepted jobs. Runtime command changes are merged into untouched form fields.

Settings are written atomically and loaded at the next launch:

- macOS: `~/Library/Application Support/Axiovox/settings.cfg`
- Windows: `%LOCALAPPDATA%/Axiovox/settings.cfg`
- Linux: `$XDG_CONFIG_HOME/axiovox/settings.cfg`, or `~/.config/axiovox/settings.cfg`

`--config path` uses that file instead for menu saves. A failed save leaves the
existing settings and pending draft intact and displays an error. User preferences
are separate from the publishable `bin/` directory.

## Console and runtime commands

Press `/` to open the console with all commands listed alphabetically. Typing
filters the list. Up/Down moves the highlighted suggestion and wraps at either end;
the list scrolls to keep selection visible. Tab accepts the highlighted name and
adds a space for values; Shift+Tab selects the previous match. Enter completes a
partial/selected name first, then executes a completed command. An exact typed
command can execute immediately. Completion preserves existing values when editing
the command name.

Left/Right, Home/End, and Backspace/Delete edit the UTF-8 line. Alt+Up/Down recalls
history; plain Up/Down recalls history when no suggestions are shown, such as while
entering values. Escape closes the console. Console capture suppresses gameplay
input queries. Recent command output fades after eight seconds when the console closes.

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
`log.level` (0 debug through 3 error), `ui.scale`, and `ui.hotbar.rows`. A setting with no value reads its current
value; one value writes it after validation. Width/height/log level apply live.
Device and worker count changes take effect without restarting the game.
Device `-1` auto-selects; other indices refer to the current GPU enumeration and
are not stable hardware identifiers. Unavailable explicit selections fail startup.

Config files contain `key=value` lines, blank lines, or `#` comments; no whitespace
around keys. Loading validates the entire file before changing settings, then commits all values
before emitting change events.

```sh
./bin/axiovox --config game.cfg --command "window.width 1600"
```

Config files load before initialization. Repeatable `--command` arguments execute
after initialization using the same dispatch as the interactive console.

## Live service changes

Commands, menu Apply, and config-file loading share one transaction path. New
workers are reserved on standby, and the candidate GPU's queues, synchronization,
font atlas, pipelines, buffers, and render-node states are prepared before replacing
the running services. Presentation changes happen outside command recording, with
the old GPU idle. The old device resources remain available until the save succeeds.
Preparation, activation, or save failures cancel the change and restore the prior
runtime. An unrecoverable device/rollback failure stops rendering and exits cleanly.

The job-pool handle remains stable across changes. Shrinking lets retiring workers
finish their current jobs; remaining workers continue the same accepted queue.
A short pause while joining retiring workers or rebuilding graphics is expected.

Render nodes with GPU resources register `create_device`/`destroy_device` hooks and
receive their current device state through `AxRenderFrame.device_state`. CPU-owned
scene data belongs in the persistent `user` object. Prepare callbacks must not
mutate live GPU state or retain temporary renderer pointers; destruction must accept
NULL/partial states. Nodes registered without hooks must be CPU-only. UI uses this
same lifecycle, and the final UI node still records last.

The hotbar and bottom-left buttons share a top edge at every drawable size. On
narrow viewports, the centered hotbar shrinks to keep clear of the icon strip.

## Architecture

- `core/`: synchronous typed events, logger, config, command registry, jobs, input,
  and GLFW window management. Input separates physical keys and Unicode text;
  focus loss releases held keys/buttons. Mouse position, button state, and scroll
  are available. Raw state is available to UI; gameplay queries honor capture.
- `app/runtime.*`: stages, activates, finalizes, and rolls back live service changes.
- `renderer/`: compatible GPU enumeration/ranking, device/surface/swapchain,
  two frames in flight, explicit image transitions, resize/out-of-date recreation,
  and dynamic rendering without render passes or framebuffers. Discrete GPUs rank
  before integrated, virtual, then others; ties follow enumeration order.
- Render nodes may record an optional prepass before the swapchain scope opens
  (their own barriers and complete rendering scopes, e.g. offscreen targets), then
  record in registration order inside a single color rendering scope;
  background is the initial scene node, UI is always last. Scene nodes can be enabled
  at runtime. Node callbacks borrow frame context and must not change the registry,
  end rendering, or perform transfers/compute requiring another rendering scope.
  This is an ordered node pipeline, not an automatic resource/dependency graph.
- `world/`: scene-owned planet recipes and solid edits, precise coordinates, atomic
  saves, indexed surface extraction, transition cells, immutable parallel streaming snapshots, composed noise terrain, multi-body gravity, mesh optimization,
  and packed render geometry (`axiovox_world`). Scene volumes do not depend on a
  renderer. `axiovox_world_renderer` owns Vulkan vertex/index buffers, frame metadata,
  depth/color/shadow targets and raster pipelines, with device migration and resize
  support. The camera remains a GPU-independent free-flight controller.
- `ui/`: console state/editor, GPU-independent draw list and HUD layout, and a
  separate Vulkan UI node. The R8 font atlas uses
  the Causality stb_truetype approach and embedded Departure Mono Nerd Font Mono, with
  licenses retained under `assets/fonts/`. Atlas glyphs cover ASCII/Latin-1; other
  Unicode codepoints are preserved in command text and displayed as `?`. Font metrics span
  14 logical pixels, baked at 28 physical pixels with nearest sampling to preserve
  Departure Mono's pixel grid at 2x display scaling.
- `ax_command_invoke` accepts argv directly, independently of parsing or the UI.
  Future Lua bindings can use this dispatch and config validation. No Lua is linked.

Events, config, commands, input, window, renderer, and console belong to the main
thread. Event payloads are borrowed only during callbacks; recursion is capped at
32 levels. Subscriber owners and the event bus must remain valid during dispatch.
The logger serializes thread-safe stderr output. The worker pool uses native
Windows/POSIX threads, a 256-job bounded queue, and explicit submission backpressure.
Job data remains caller-owned until completion. Workers must return normally and
must not wait/destroy their own pool or access main-thread services directly.
Shutdown drains workers, waits for GPU work, and destroys UI, world node, renderer,
packed geometry, scene, then window.
