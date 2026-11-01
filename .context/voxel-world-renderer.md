# Voxel world renderer, contours, and camera

## Scope / status

World renderer from ~/Downloads/laine2010tr1_paper.pdf (Laine & Karras, ESVO, NVR-2010-001):
full-screen triangle + per-pixel SVO ray cast (Fig. 15 / Appendix A) with contours
(Sections 5.2-5.3), free-flight camera, temporary hardcoded SDF scene at 1024^3 / 64 m.
No commits/pushes. Not yet done from the paper: compressed attributes (5.5), beam
optimization (6.2), post-process filter (6.3), blocks/streaming (7).

## Boundaries

- `core/input.*`: per-frame `mouse_dx/dy`; first sample after invalidation (lock change,
  focus loss) yields no motion. `ax_input_mouse_delta` only when locked and not captured.
- `core/window.*`: `ax_window_set_cursor_locked`; main locks when `!captured && focused &&
  !iconified`. HUD icons skip hover/click while `pointer_locked`.
- `renderer`: `ax_renderer_remove_node`, `ax_buffer_upload` (staging + barrier).
- `world/camera.*`: right-handed Y-up, yaw 0 → -Z; W/S along view, A/D strafe, E/Q world
  up/down, Shift x4; defaults fov 70°, 6 m/s, 0.0025 rad/px; dt clamped 0.1 s.
- `world/octree.*`: input is `AxVoxelSource` (thread-safe SDF + color callbacks, extent,
  depth, lipschitz). Surface-only (hollow shells). Split level 3 → 512 subtree tasks on
  AxJobs (falls back inline when queue full/NULL), arenas merged with index rebasing, top
  levels combined on owner thread; output bit-identical to serial (tested).
  Cull: |f(center)| > L*0.866*s → empty. Leaf: 8 corners + center; valid if sign change
  or a Newton-projected sample within 0.25 s; contour normal = corner gradient, range from
  projected samples + 0.12 s margin; dropped if slab misses cube. Node contour: gradient at
  center, range = extents of children's cube∩slab polytopes (vertex enumeration) + 0.01 s,
  so every leaf polytope is inside all ancestor slabs. Slab stored only if it cuts the cube
  (2*thick < 0.95 width). Encoding paper Fig. 4; normals scaled to max component 31.
  Words: [desc 2w][lookup 1w][values][contours]; desc word1 = contour ptr<<8 | mask.
  `ax_octree_sample(p world)` applies contours exactly like the GPU.
- `world/demo_scene.*`: TEMPORARY. All components 1-Lipschitz: terrain heightfield
  (max(H, water)) * 0.36 plus exact bounds above 10.5 m / below 5 m; foliage
  max(k*(sphere+noise), sphere-A); capsules; ruin. Trees in 16x16 XZ buckets; unlisted
  trees bounded by a 2 m cap. Color per leaf cell (tiny-voxel jitter, leaf ramp, water,
  sand, slopes, flower specks). Built in main BEFORE window creation: with a window, the
  blocked main thread gets the process throttled by macOS (13 s vs 2.1 s).
- Contours are compile-time, default OFF: CMake option `AXIOVOX_WORLD_CONTOURS` →
  `file(CONFIGURE)` writes `<build>/generated/world_config.h` (`AX_WORLD_CONTOURS 0|1`),
  public include of axiovox_world and `#include`d by world.frag (glslc -I generated,
  DEPENDS on the header so toggling rebuilds shaders). Off: builder skips leaf/node
  contour fitting (leaves need a sign change), all contour masks zero, shader omits the
  intersection. Runtime toggle command removed.
- `world/world_renderer.*`: node "world", per-device SSBO/pipeline, 112-byte push
  constants (eye in [1,2]^3, scaled basis, footprint, sun + shadow bias 0.75 voxel,
  offsets, step budgets 1024/512, contour offset).
- `shaders/world.frag`: Appendix A contour intersection (exact bitfield decode instead
  of CUDA shift trick), hit normal tracked from whichever plane last raised t_min (root
  entry, ADVANCE face, contour plane).

## Validation

- Release: all 9 CTests passed (world test includes sphere/gyroid hole-free enclosure on
  40k true surface points, off-surface exclusion, far pointers, parallel determinism, demo
  scene). World tests pass under ASan/UBSan (~70 s).
- GPU screenshots blocked (no screen-recording permission); a line-for-line C port of
  the shader (scratchpad only) rendered spawn and contour on/off close-ups: smooth walls
  and terrain with contours, cube staircases without, no holes. Minor stepping remains
  at concave wall/ground junctions (single contour per voxel; paper notes sharp edges).
- Contours OFF (default): 762k descriptors, 3.06M values, ~20 MiB, 1.7 s (4 workers); all 9
  CTests pass. Contours ON verified in a scratch build (world tests pass, shader compiles).
- Contours ON demo: 824k descriptors, 3.55M values, 3.55M contours, ~37 MiB, 2.1 s with 4 workers
  (7.7 s single thread, 1.4 s with 8). Windows/Linux unverified.
