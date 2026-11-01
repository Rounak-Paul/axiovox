# Window size synchronization

## Cause and fix

Manual native resizes changed GLFW/framebuffer state but never configuration;
slash reads, saves, and Video settings kept the old dimensions.

- `core/window.c`: `ax_window_sync_config` reconciles GLFW logical dimensions
  as one config transaction. Called after event polling before UI/command input,
  and at startup before CLI commands. Framebuffer pixels remain renderer-owned,
  preventing HiDPI dimensions from leaking into logical window settings.
- Native size limits match config bounds (64–16384). Iconified/zero-sized windows
  retain their last usable settings. Unchanged dimensions emit no notifications.
- `main.c`: config observer compares actual dimensions before requesting a resize,
  preventing feedback and redundant native resize calls for width/height events.
- Existing menu merge updates untouched draft fields while retaining user edits.
  Other settings (HUD, logging, GPU and workers) already have application paths;
  GPU Automatic remains a selection policy, not a physical device index.
- Resizes update memory; explicit menu Apply/config save persists current values.
  No automatic disk writes during window dragging.

## Validation

Release build passed. Runtime regression uses a real GLFW window, reconciles a
nonpreset logical size, checks both config dimensions and active values, confirms
menu refresh retains an edited UI scale, and checks repeated sync emits no events.
All eight Release CTests passed, including Vulkan presentation/runtime integration.
`git diff --check` passed. Native drag interaction was not separately automated;
the regression exercises the same GLFW size state through a native resize request.
