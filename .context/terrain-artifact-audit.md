# Terrain artifact audit

Current authority for cave LOD, leaf budget, surface color and shadow casting. Older
terrain notes describe earlier states; this file wins where they disagree.

## Method

Prior passing tests missed these defects, so diagnose against the real planet: a scratch
probe (not in the repo) links the release libs, builds the default Terra/Luna scene, renders
through the real pipeline, and counts folded triangles (geometric vs smooth normal dot<0.5).
Isolate by disabling one stage at a time. Every fix below was mutation-checked.

## Root causes and fixes

- Sawtooth notches and grey blotches: cave channels undersampled at coarse LODs. A lattice
  point landing in a tube drops the surface by a cell-wide pit (up to 25 m measured). Not
  caused by Transvoxel, simplifier or displacement. Folds fell 10,883 -> about 1,500 at the
  reference view with caves omitted. `surface.*`: leaves carve caves only when spacing
  <= AX_SURFACE_CAVE_SPACING (2 m, extent 32 at 16 cells). Authoritative `ax_planet_sample`
  always includes caves; meshing uses `ax_planet_sample_lod`.
- Cave/no-cave seam: `sealed_neighbors` is a 26-direction mask (part of the mesh reuse key).
  Boundary samples touching a coarser non-resolving leaf omit caves, so walls cap at the
  plane. Must cover edges/corners: a face-only rule cracks (test proves this).
- Dashed lines on triangle creases: shadow receiver-plane extrapolation at concave creases.
  Shadow pass now culls front faces (casts from surfaces facing away from the sun), so lit
  terrain cannot self-shadow; receiver-plane math removed, plain bias 0.0002 (about 10 cm)
  clears tiny back-facing seam triangles. Also bilinear PCF, range fade, world-stable texels
  (light_right.w/light_up.w offsets; info.y carries the span).
- Jagged material borders: hard per-vertex classes. `ax_planet_surface_color` blends climate
  and slope continuously. Optimizer no longer locks color-change edges (would lock all).
- Polygonal Luna and missed fine leaves: conservative bounds treat +-relief as surface and one
  failed split aborted all refinement. `ax_planet_surface_likely` (non-conservative; refinement
  only, never geometry omission) drives `settled`; `uniform` stays conservative for balance and
  transition masks. Failed splits roll back via undo log and refinement continues. Leaves larger
  than body radius refine first. AX_WORLD_MAX_REGIONS 4096: about 2/3 are empty and undrawn.
- Banding: composite pass dithers +-0.5/255.

## Planning

Merge hysteresis uses a hashed refined-cell set rather than an O(N^2) scan. Replan distance
is AX_STREAM_REPLAN_DISTANCE (2 m).

## Known costs and limits

See measured numbers in the final validation section. Mesh rebuilds dominate settle time,
and most rebuilt leaves are empty but still sample a full lattice. Cheap emptiness rejection
would need a true Lipschitz bound; the analytic worst case is about 16, too loose above
extent 256, and an estimate there would risk holes. Publication still drains the device. Caves are capped at the 32/64 m LOD boundary, so a
tunnel seen from inside ends at that distance until the camera approaches.

## Final validation

Release: all 14 CTests pass. ASan/UBSan: surface, scene, procedural, world, stream pass. TSan:
stream passes. `git diff --check` passes. App ran 12 s without exiting. No commits or pushes.
Regression tests: `sealed_cave_seams`, `coarse_terrain_has_no_pits` (surface), concave crease
acne (world_render), Luna floor (stream), `ax_planet_surface_likely` (scene). Each of these
reverts fails its test: caves at all LODs, sealing ignored, sealing faces only, front-face
shadow casters. Visual sweep: 8 Terra sites from 6 m to 20 km, plus Luna from Terra's surface;
the final state was viewed at the spawn and 4 of those sites (material blending changed after the rest).
Cave interiors were checked by closure tests only, never viewed.

Measured on M3 Pro, default scene, 12 m altitude, 8 workers (previous commit in brackets; its
layout was stuck at the old cap, which flatters its move times):
startup 3.0 s (1.8 s); settle after 2 m move 178 ms (14 ms), 32 m 661 ms (153 ms), 512 m
1644 ms (381 ms). About 3,500 leaves, 1,080 drawn, 10.4 MiB. Publish 8.4 ms on an idle GPU.

Not verified: continuous flight by eye (popping, publish stutter under GPU load), Vulkan
backends other than MoltenVK. Remaining folded triangles are about 1% of the mesh (cause not
isolated, transition cells suspected) and were not visible in the sweep. Very faint normal differences at some LOD
planes remain visible only on close inspection.

## LOD cross-fade

Popping is hidden by blending complete layouts, not by vertex morphing: marching-cubes vertices
have no LOD correspondence and morphing could reopen seams. `world_renderer.c` keeps two `Cut`s
(buffers plus own region table). During a fade each renders its own shadow and color; depth and
shadow targets are reused sequentially, only color is doubled. Composite mixes by `world->fade`
(1 to 0 over 0.25 s). `main.c` holds a dirty stream until the fade ends; only the newest layout
publishes. Teleports use `set_geometry` (instant); debug views never fade. Device migration ends
a fade. Outgoing buffers stay allocated until the next publish, about 10 MiB extra, plus one
window-sized color target.

Frozen-camera streaming, real async path, 40 m altitude, 2800x1800, worst single-frame channel
delta (pixels over 4 levels in the worst frame), snap vs fade:
30 m/s 30 (216) vs 2 (0); 300 m/s 119 (35163) vs 8 (17). Frame pacing unchanged at about 7.3 ms
on M3 Pro, but that is display-paced, so it is not a GPU cost measurement: terrain draws double
while fading. Not verified by eye in flight.

Validation layers (test relinked through the loader; the default build links MoltenVK directly, bypassing
layers): world_render, which covers fades, resize and device migration mid-fade, reports only
VUID-VkDescriptorImageInfo-mutableComparisonSamplers-04450. It predates the fade: the shadow comparison
sampler is written as a mutable descriptor. Fix would be an immutable sampler on binding 1. Not done.
