# Published terrain meshing correction

Cave LOD, leaf budget, surface color and shadow casting: `terrain-artifact-audit.md` supersedes this file where they disagree.

User reports remaining terrain defects after distance-LOD changes. Prior passing tests
were insufficient evidence of visual correctness. Compare actual implementation to
Eric Lengyel, Voxel-Based Terrain for Real-Time Virtual Simulations (2010), chapter4,
https://transvoxel.org/Lengyel-VoxelTerrain.pdf and author tables at
https://github.com/EricLengyel/Transvoxel. Godot Voxel Tools is a maintained reference
implementation: https://github.com/Zylann/godot_voxel/tree/master/meshers/transvoxel.

Initial discrepancy: current transition contours are collapsed onto one plane, and
regular boundary vertices never move to provide space. Inspect published deformation,
edge/corner behavior, vertex reuse, interpolation and gradient rules before changing.

## Implemented correction

The author overview describes finite transition cells; Voxel Tools provides the
maintained fine-side implementation used to cross-check the deformation and masks.
Primary reference source inspected:
https://raw.githubusercontent.com/Zylann/godot_voxel/master/meshers/transvoxel/transvoxel.cpp
and its smooth_terrain.md secondary-position mask rules. The dissertation PDF could
not be downloaded through this session's network; do not claim it was read in full.

`surface.c` now computes quarter-cell offsets within the outer cell band, projected
onto the normalized density-gradient tangent plane. Regular and full-resolution
transition vertices use the same secondary position. Half-resolution transition
vertices remain on the shared coarse contour. Vertices touching an inactive block
border stay pinned, avoiding new cracks at same-LOD edges and corners. Material
classification still samples the undeformed source point. This is the established
fine-side variant, rather than the thesis's coarse-side ownership convention.

Fixed endpoint caching: coincident coarse-side and fine-side vertices must remain
distinct when their primary positions match but secondary positions differ. Only
movable fine-side endpoints participate in the regular corner cache.

## Validation

- New six-face island fixture requires quarter-cell transition thickness and closed
  topology. Confirmed the previous implementation fails this test at face0.
- All256 refinement patterns of the eight parent octants pass oriented closure after
  optimization, tested both on a generic sphere and a grid-aligned sphere (512 cuts).
- Existing extraction, sculpted cavity, normals and optimized seam fixtures remain.
- Actual million-meter-radius continental terrain was rendered/read back in all3
  views; temporary diagnostic captures are `/tmp/ax-terrain-{0,1,2}.ppm` (0 default,
  1 wireframe, 2 normals). One surface view is a visual spot check, not proof that
  every terrain appearance issue has been eliminated. Temporal LOD popping and
  under-resolution of thin cave features remain possible.
- Final Release build and all14 CTests pass (13.8 s). ASan/UBSan surface tests pass,
  including the six-face thickness fixture and512 adaptive configurations.
  `git diff --check` passes. No commits or pushes.
