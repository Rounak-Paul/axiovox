# Planet terrain, gravity and mesh optimization

Streaming/surface details below are historical. Current distance-based adaptive chunks,
face masks, normal filters and shadows: `terrain-artifacts-distance-lod.md`.

## Scope and decisions

Implemented composed planetary terrain, gravity-aware free flight, and low-polygon
terrain optimization. User clarified: flight remains free throughout; gravity aligns
controls near bodies, then alignment fades continuously into space. No falling or
collision controller. No commits/pushes; QuasarEngine was inspected read-only.

## Owning modules

- `math/noise.*`: immutable seeded permutation contexts, gradient noise with analytic
  derivatives, value noise, normalized rotated fBm/ridged octaves, cellular distance,
  three-channel domain warp and deterministic seed hashing. Value-only consumers avoid
  derivative work. Cellular jitter is restricted so the27-neighbor search covers F1.
- `world/terrain.*`: validated immutable recipe. Continental masks/warps compose
  basins, mountain provinces, ridges, hills and detail; crater profiles combine bowls,
  rims and rough highlands. Spherical projection has no map-face/polar seams.
  Cave channels stay within a depth band with a6 m roof. Climate uses latitude,
  elevation and moisture. These are noise landforms, not erosion/drainage/fluid solves.
- `scene.*`: planet caches its terrain contexts. Conservative radial-envelope and CSG
  interval bounds replace the former sine-gradient cull assumption. Exposed-surface
  materials are queried separately from buried occupancy, avoiding noisy rock aliases
  on coarse meshes. Slope and caves expose rock; climates add sand/snow.
- `surface.*`: one-cell density halo; vertex normals interpolate cached central
  differences. No repeated Newton/noise resampling per vertex. Raw indexed extraction
  remains independent of the streaming optimizer.
- `mesh.h`/`mesh_optimizer.*`: scene-independent CPU mesh types and exception-safe C
  interface over vendored meshoptimizer1.0 (pinned MIT source subset). Deduplicate,
  remove finer-covered geometry, simplify with normal/color metrics, cache reorder,
  fetch compaction. All replacements are staged before publishing owned C allocations.
- `stream.*`: region jobs run independently on the existing pool, with inline queue
  backpressure fallback. No worker waits. An acquire/release completion counter commits
  an entire coherent layout only after every job succeeds with current scene revisions.
  Immutable borrowed meshes survive until completion. Serial/parallel complex-terrain
  fixture output is checked byte-for-byte.

## Optimization constraints

Target15% of visible input triangles, under a0.15-cell absolute simplifier error
metric, with topology/material constraints allowed to stop reduction earlier. Remove
triangles inside the active finer cube before simplifying. Preserve outer transition
faces and a two-cell halo around active inner/outer seams. Parent caches invalidate
when the inner cube moves; no blanket locking of all potential future planes.

Hysteresis keeps detail origins while the eye stays within the central37.5..62.5%
range. Adjacent detail levels remain nested/aligned. Whole-body roots may intersect
only part of a finer cube; trimming handles that intersection and ignores nonoverlap.

## Gravity and camera

`gravity.*` sums configured body acceleration: inverse-square outside, linear inside
uniform-density spheres, zero self-gravity at the center. Uses precise frame-local
positions, handles cancellation/zero fields, never divides by zero. Terrain edits do
not redistribute planetary mass.

Alignment influence smoothly fades between0.25 and0.02 m/s². `camera.*` transports a
local tangent frame with minimal rotation, retaining heading through poles and using
a stable antipodal axis. Time-based alignment freezes in weak fields; W/S remain
view-axial, A/D tangent strafe, E/Q local-up. Gravity does not accelerate the camera.
Visits initialize the frame at a terrain-aware temperate site. Cruise speed uses
terrain clearance, not mountain elevation above the reference radius.

Defaults: Terra seed42 radius1,000 km relief6.5 km, continental terrain, caves128 m,
surface gravity9.81. Luna seed917 radius270 km relief2.5 km, cratered rock, gravity1.62.
Centers remain static; orbital simulation is outside this task.

## Persistence and cache identity

AXSCENE3 adds terrain kind, cave depth and surface gravity. Versions1/2 retain their
original wave field (golden-value regression); scenes still save atomically and load
transactionally. Legacy geometry is not silently regenerated with the new recipe.
Scene load increments a runtime epoch. Snapshot checks and cache reuse require epoch
matches, preventing stale acceptance when a loaded scene reuses IDs/revision counts.

## Validation and measurements

- Final Release build and all14 CTests pass, including the epoch reload regression.
- CPU tests: analytic noise derivatives/ranges/seeds, spherical height invariance,
  varied landforms, volume bounds, gravity falloff/interiors/cancellation, camera
  transport through poles and frozen space orientation, save-format migration.
- Surface tests: oriented closed manifolds across optimized2:1 seams and excavations,
  error/invalid-input behavior; near-flat fixture8,192 ->1,232 triangles (~85% fewer).
- GPU readback: optimized planet geometry, cavities, actual shadows compared on the
  same geometry, non-Y-up view, resize, device migration and failed-publication retention.
- App smoke passed Terra/Luna visits, sculpt/build, space positioning, GPU migration,
  AXSCENE3 save/load, and bundled optimizer license presence.
- Default warm-site sample:389,122 extracted,310,142 visible after trimming,~196.7k
  optimized triangles (~49% fewer overall;~37% reduction of visible geometry),~5 MiB
  packed data. Startup timings varied with concurrent checks; no frame-time claim.
- Final ASan/UBSan CPU suites pass, including complex terrain/epoch streaming (97 s
  under instrumentation). TSan streaming passes (93 s). Release streaming is3.5 s
  in this validation run. These test costs are not frame-time measurements.

## Remaining limits

Spatial edit indexing, asynchronous/partial GPU publication, temporal LOD morphing,
ocean fluids, erosion/drainage and orbital dynamics are not implemented. Allocation
exceptions preserve source meshes by implementation; allocation-failure injection was
not performed. Windows/Linux builds remain unverified in this macOS session.
