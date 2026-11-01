# Terrain artifacts, debug views and distance LOD

Follow-up: `published-terrain-correction.md` supersedes the former zero-width transition
assumption. Regular boundary vertices now make finite space using tangent-projected offsets.


## Request

Fix screenshot's patterned surface/region artifacts, add default/wireframe/normals
commands, and make terrain LOD follow camera distance in every direction.

## Confirmed owning paths

- Shadow bias used smooth shading normals, not the actual rasterized receiver plane;
  shadow texel quantization can exceed the bias on slopes and create moire/checkers.
- Streaming used nested camera-centered boxes and nominal-radius nearest-body choice.
  Replace with fixed world chunks selected by 3D point-to-box distance, a balanced
  adaptive cut, and shared face-only transitions. No altitude selection policy.
- Cached normal filters differed across LOD faces. Transition normals must match the
  coarser lattice filter, with a two-cell halo/blend on the finer side.

## Current implementation

- `stream.*`: fixed octree leaves, all planets, Euclidean camera-to-AABB distance.
  Split at1.25*extent; previously refined nodes merge beyond1.5*extent. Finest extent8 m,
  16 cells/side (0.5 m). A2,048-leaf cap bounds resources; priority refinement rolls back
  its last split if balancing would exceed the cap. Roots always preserve coverage.
- Open-addressed hierarchy lookup finds coarser neighbors on the exact power-of-two
  lattice. All26 touching neighbors balance2:1; only fine faces adjoining a coarser
  non-uniform neighbor emit transition cells. Bounds-proven uniform cells need none.
- Planning and changed-region meshing run on the existing pool with immutable source
  snapshots and borrowed live meshes. Owner drains accepted work before destroying
  metadata. Queue saturation defers submissions, never inline work on the render thread.
  Startup is synchronous. Scene epoch, body set and revisions reject stale publication.
- Mesh reuse keys: epoch, planet, revision, fixed origin/extent, transition mask. Unchanged
  layouts only advance the planned eye; no redundant GPU publication. Replan after1 m
  displacement or source/frame change. Completed layouts publish coherently.
- `geometry.*`: dynamically allocated packed region ranges; disjoint leaves eliminate
  density-overlap trimming and per-fragment finer-region scans in the runtime path.
  Optional exclusion remains a generic optimizer utility with independent regression tests.
- `surface.*`: two-cell density halo; central-difference gradients interpolate on fine
  samples, blending to the coarser lattice over two fine cells at transition faces.
- `world_renderer.*`: `/renderer.view [default|wireframe|normals]`, strict argument checks,
  query with no argument. Portable line-list edge buffer is built lazily; a triangle
  depth prepass hides rear edges. Wire buffers follow geometry/device replacement.
  Normals show world-axis RGB; debug sky is dark. No optional Vulkan polygon-mode feature.
- `world.frag`: receiver-plane depth derivatives correct each nearest PCF texel's depth,
  rather than approximating geometry slopes with the smooth normal. Small constant
  bias covers depth roundoff. Derivatives precede divergent lighting/debug branches.

## Validation

Final Release build and all14 CTests pass. Regression coverage includes disjoint adaptive seam closure in both octants, 2:1/nonoverlap/mask checks,
lateral approach/retreat at fixed height, body additions, stale reload/edit rejection,
serial/parallel identity, debug-mode readback, wireframe geometry/device replacement,
and a sloped two-triangle receiver with zero shadow-on/off pixel differences above3
in the central viewport. Existing raised-block shadow test still passes.

ASan/UBSan: all5 CPU terrain/world suites pass with -O1 (stream19 s). The original
unoptimized sanitizer stream run exceeded180 s without reporting a sanitizer error;
the optimized rerun keeps the same sanitizers. Final TSan streaming passes with -O1 (118 s), including asynchronous planning,
queue pumping, stale source rejection and pending-task destruction. App smoke also passed normals, Luna visit,
space altitude and wireframe in one session. `git diff --check` passes.

A standalone full-size Terra/Luna probe reached8 m leaves within2,004 leaves.
Planning measured about16 ms on this Mac before moving planning to a worker; this is
not a frame-time guarantee. Startup meshing remains synchronous. GPU buffer publication
still waits for device idle, temporal LOD changes can pop, and edit indexing is unchanged.
