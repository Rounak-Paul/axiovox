# Solid planet scene model

## Ownership and coordinates

- `scene.*`: AxScene owns heap-stable planets with nonzero uint64 identity, procedural
  seed, system-relative double center, radius, terrain parameters and surface material.
  No renderer, triangles or octree types in the scene API.
- `coordinates.*`: AxPosition stores int64 address[3] plus double meter offsets in
  [0,2). The 2 m addressing unit is save-format state, independent of voxel resolution.
  Negative positions floor correctly; checked movement leaves state intact on overflow.
  Integer differences precede float conversion to preserve nearby precision.
- Camera origin is precise planet-local position; float displacements integrate and
  rebase each frame. Mesh origins transform relative to the camera, not a global float.

## Solid volume

Negative-inside radial field plus seeded product-of-sines displacement. Entire
interior is implicit, not a hollow surface. Ordered sphere/box CSG edits support
excavation and building materials. Queries return air/rock/soil/grass/building.
Surface material can override the default layering (Luna uses rock).

AX_EVENT_WORLD_CHANGED carries planet/revision after commit. Mutations during its
notification are rejected. Scene cloning deep-copies edits/revisions without event
subscriptions, allowing immutable worker sampling while the owner thread edits.

## Persistence and defaults

AXSCENE version2 text stores recipes and edits with exact hexadecimal floats, still
reads version1. Atomic save through core/files; bounded/range-checked parsing rejects
duplicates, malformed/truncated data and overflow transactionally. Render caches and
camera pose are not persisted. Load emits a reset notification (planet0).

Terra id1 seed42 radius1,000 km at0. Luna id2 seed917 radius270 km at
(1200,1800,-2400) km, static initial placement. Names correspond to these built-in IDs.

Commands: world.planets, world.visit ID, world.carve R DIST [sphere|box],
world.build R DIST [sphere|box], world.save/load PATH, world.speed, world.altitude.
Brushes act ahead of the camera on the nearest body, independent of its reference frame.

## Current rendering and remaining work

See `triangle-terrain-refactor.md`. Solid CSG is authoritative; extracted triangles
are disposable render assets. Existing save semantics are preserved by the refactor.
Remaining: spatial edit indexing/compaction, character collision/gravity, orbital
motion, galaxy sectors, camera persistence, and richer independently oriented buildings.
