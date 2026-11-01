# Live device/worker settings and HUD alignment

## Scope / status

Implemented live GPU/worker settings and hotbar alignment. User explicitly removed
restart-only requirements for these settings. No commits, pushes, or index changes.
This supersedes restart behavior in older project context files.

## Boundaries

- `core/jobs.*`: stable pool handle and accepted queue. Staged growth creates standby
  worker slots; cancel joins only those candidates. Shrink retires workers after
  their current job, leaving accepted queued work to surviving workers. Resize/wait/
  destroy remain owner-thread operations, never worker callbacks. Complete a staged
  change before destroying the pool. Submissions and worker-count reads are locked.
- `core/config.*`: runtime hook transaction prepare -> activate -> atomic save ->
  publish all values -> finish -> CONFIG_CHANGED. Draft helper strips hooks/events
  so editing a menu cannot change services. Failure restores old runtime before
  returning false; error text is available to menus/commands. Device and workers
  metadata now live, while generic restart fields remain available for future keys.
- `app/runtime.*`: coordinates job/GPU change tokens and exposes health. Rejects
  service changes from render callbacks or while another device change is pending.
  Fatal rollback failure exits the game before another draw.
- Renderer retains instance, surface, device catalog and physical identities across
  device changes. New logical device/queues/format/commands/fences and per-node GPU
  state prepared separately. Old swapchain released only while idle before new
  swapchain creation, avoiding native-window ownership conflicts across VkDevices.
  Old device-local state retained until save/commit; rollback reconstructs its
  swapchain, retaining the old pipelines/buffers/textures. Selection event emitted
  only after a successful commit and after config values are published.
- Render node lifecycle callbacks own a distinct device_state; record receives it
  through AxRenderFrame. Persistent user data remains CPU-owned. Callbacks must not
  retain temporary renderer addresses/mutate registry; destroy accepts NULL/partial
  state. UI refactored into persistent console/HUD/menu model and device-local atlas,
  descriptor, pipeline, and frame-buffer resources using these same hooks.
- Explicit GPU APIs/staged tokens are main-thread only and must finish before window,
  renderer, or bound CPU objects are destroyed. Candidate GPU work is drained before
  failure cleanup, and old GPU work is drained before presentation transfer.
- Hotbar panel top now matches icon panel top; narrow hotbars shrink horizontally
  rather than jumping above icons. Row selection/scaling and two icons retained.

## Validation

- Release build and all eight CTests passed: core, console, UI, menu, renderer,
  presentation, runtime integration, live presentation with commands.
- ASan/UBSan tests passed for core/console/UI/menu/runtime including final cleanup edits. ThreadSanitizer core tests passed with live pool resize/cancel.
- Runtime GPU tests recreate real VkDevices on Apple M3 Pro, migrate UI and a real
  per-node buffer, inject partial node-prepare failure, reject unavailable index,
  and force a save failure after candidate presentation activation. Old device,
  node/UI states, accepted jobs, config values, and selection-event counts verified.
  Valid combined save/device/worker changes tested and read back from disk.
- Console history, paused/settings state, edited draft, active hotbar row/selection
  preserved across GPU swaps. Test resource create/destroy totals match at shutdown.
- Geometry assertions verify matching tops at 1280x720 and 320x200, alongside prior
  clipping checks at all UI scale levels. bin/axiovox remains the sole game binary.
- Hardware has one physical GPU: tested Auto/explicit device recreation and rollback,
  not migration between different physical GPUs. Windows/Linux execution and
  catastrophic device-loss/rollback-failure injection remain unverified.
