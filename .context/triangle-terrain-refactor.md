# Triangle terrain refactor

## Accepted direction

Retain authoritative solid voxel/density planets, precise coordinates, edits, saves,
and a single continuous Terra/Luna scene. Replace the ESVO surface renderer with
rasterized triangle surfaces. User authorized a clean refactor; no commits/pushes.

## Implementation

- Transvoxel-compatible modified marching cubes and transition tables from Eric
  Lengyel's MIT-licensed repository, pinned under vendors/transvoxel with provenance.
- Indexed region-local surface meshes from the existing scene volume; shared lattice
  samples/edge vertices. Adaptive resolution replaces the former global tiny-voxel goal.
- Coherent nested LOD layouts built asynchronously from immutable scene snapshots.
  Reuse unchanged meshes; publish all changed levels together to keep seams consistent.
- Transition cells join 2:1 boundaries; density and material remain scene-owned.
- Vulkan triangle rasterization with camera-relative positioning, reverse-Z depth,
  directional shadows, atmosphere/space background and compositing before UI.
- Removed obsolete octree/demo/beam paths after migrating the checks.

Existing uncommitted core/UI changes were preserved. Older renderer context
documents now point here rather than describing removed APIs.

## Implemented boundaries

- `surface.*`: 64-cell indexed regions, regular lattice sample/edge/corner reuse;
  smooth density-gradient normals and projected material lookup. Default spacing
  0.5 m over32 m. All 6 faces stitch to the parent using zero-depth Transvoxel cells
  on shared planes, without shifting regular surface vertices.
- `stream.*`: up to40 nested levels, powers-of-two extents; every neighboring active
  level has2:1 sample spacing. Roots use the same64 cells. Planes align to parent
  lattice. Rejects render extents beyond32*2^40 m rather than silently leaving LOD gaps.
  Main thread plans immutable snapshots; worker builds only changed meshes. Borrowed
  cached meshes survive until the task completes; whole-layout commit transfers
  ownership atomically. Stale scene revisions discard the entire candidate.
- `geometry.*`: packed indexed draw ranges and precise region placement. Empty regions
  retain ownership masks so coarse meshes cannot fill edited empty space.
- `world_renderer.*`: indexed rasterization, backface culling, infinite reverse-Z D32
  depth, directional1024² shadow map (local128 m half span), sky/atmosphere, composition
  into the existing swapchain scope before UI. Offscreen targets isolate depth from UI.
  Per-frame mapped metadata is fence-protected; publication prepares both buffers
  before waiting/retiring old assets. Device migration reconstructs all node resources.
- Scene API has no rendering representation dependency. Old octree/demo sources,
  beam shader generation and public octree renderer APIs removed. MIT Transvoxel
  tables/license included in the distribution; source commit pinned in vendor README.

## Validation

Surface tests geometrically weld triangle edges and require closed, consistently
oriented manifolds for a sphere and 2:1 transition seams, including a cut crossing a
LOD plane. GPU readback checks both bodies, detailed excavation ownership, an actual
cast shadow, near/far depth, resize, invalid-publication retention and device migration.
Release build and all13 CTests pass (5.4 s total on this machine). Surface, stream
and scene suites pass ASan/UBSan. Application command smoke passed carving/building,
atomic save/load, Terra/Luna visits, radial space positioning and GPU migration.
Distribution includes Transvoxel MIT license; git diff --check passes.

Initial default scene:18 mesh regions,227,873 triangles,5.7 MiB packed data,~295 ms
startup extraction in this run. These are scene/startup measurements, not a whole-game
frame-time claim. No commits or pushes.

## Explicit remaining limits

Publishing still performs a packed vertex/index upload and GPU drain. Spatial seams
are stitched, but temporal LOD changes can pop. No spatial edit index or general mesh
simplifier yet. Directional shadows cover a local volume, not whole-system eclipses.
Gravity/collision and orbital simulation remain separate gameplay work. No universal
frame-time or production-world performance claim is made from fixture/startup checks.
