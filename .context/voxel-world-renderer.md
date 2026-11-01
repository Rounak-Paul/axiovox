# Voxel world renderer and camera

## Scope / status

Implemented the world renderer from ~/Downloads/laine2010tr1_paper.pdf (Laine & Karras,
Efficient Sparse Voxel Octrees, NVR-2010-001): full-screen triangle + per-pixel SVO
ray cast (paper Fig. 15), free-flight camera, temporary hardcoded scene. No commits/pushes.
Future improvements (contours, compressed attributes, streaming/blocks, ECS scene) build on this core.

## Boundaries

- `core/input.*`: `mouse_dx/dy` accumulate per frame via `ax_input_mouse_move`; first
  sample after invalidation (lock change, focus loss) yields no motion.
  `ax_input_mouse_delta` returns motion only when pointer locked and not captured.
- `core/window.*`: `ax_window_set_cursor_locked` (GLFW disabled cursor + raw motion
  when supported). main.c locks when `!input.captured && focused && !iconified`.
  HUD icons skip hover/click while `pointer_locked` (virtual cursor is meaningless).
- `renderer`: added `ax_renderer_remove_node` (drain, destroy device state, order-preserving
  removal) and `ax_buffer_upload` (staging copy to device-local buffer + barrier).
- `world/camera.*` (lib axiovox_world): right-handed Y-up, yaw 0 → -Z. W/S along view
  (incl. pitch), A/D strafe, E/Q world up/down, Shift x4. Pitch clamped, yaw wrapped,
  dt clamped to 0.1 s, non-finite input ignored. Defaults: fov 70°, 24 u/s, 0.0025 rad/px.
- `world/octree.*`: dense `AxVoxelGrid` (uint8 palette, 0 empty) → ESVO word array:
  [descriptors 2 words each][lookup 1 word each][ABGR values]. Descriptor word0 =
  ptr[31:17] | far[16] | valid[15:8] | non-leaf[7:0] (non-leaf mask as in paper's code);
  word1 = contour slot reserved (0). Child groups: [members][far slots][member subtrees].
  Far slot word0 = offset relative to the parent descriptor. Lookup = valuePtr<<8 | mask
  (paper Fig. 7c, single block → no page headers). Values exist for non-leaf children
  too (averaged color) for LOD. Uniform solid subtrees collapse into coarse leaves.
  `ax_octree_sample` decodes exactly like the GPU (used by tests; usable for picking).
- `world/demo_scene.*`: TEMPORARY. 256³ terrain/pond/trees/ruin, spawn at (128, h+30, 244),
  trees excluded within 40 voxels of spawn. Replace when scene hierarchy/ECS lands.
- `world/world_renderer.*` (lib axiovox_world_renderer): node "world", per-device state
  (SSBO, descriptor, pipeline) re-created on device switch. 96-byte push constants:
  octree-space eye ([1,2]³), basis scaled by tan(fov/2)/aspect, pixel footprint × lod_bias,
  sun, offsets, step budgets (768 primary / 384 shadow). Checks maxStorageBufferRange.
- `shaders/world.frag`: Fig. 15 PUSH/ADVANCE/POP with s_max = 23, findMSB for POP,
  LOD termination `tc_max * footprint >= scale_exp2`, entry-face normal, shadow ray,
  hemisphere ambient, fog, sky. Step budget exhaustion = miss. Contours not implemented.

## Validation

- Release build without warnings; all 9 CTests (new `world` test + GPU presentation/
  runtime/live device switch with world node) passed. world/core/UI tests pass under ASan/UBSan.
- World tests: camera axes/boost/clamps/capture/NaN, pointer invalidation, octree
  empty/uniform/sparse round-trip, 128³ random grid with far pointers, demo spawn not solid.
- Screenshots of the GPU window are blocked (no screen-recording permission). Instead, a
  line-for-line C port of cast_ray rendered the demo octree (scratchpad only, not committed):
  correct terrain/trees/ruin/pond/shadows, hits cross-checked against ax_octree_sample.
- Demo build: 127k descriptors, ~5 MiB, ~64 ms Release. Validation layers not loaded
  (bin links MoltenVK directly). Windows/Linux unverified.
