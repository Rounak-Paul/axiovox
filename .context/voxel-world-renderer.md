# Historical voxel ray renderer

Superseded by `triangle-terrain-refactor.md` following the user's explicit approval
of density-based terrain with triangle rendering (2026-11-01).

The former backend implemented Laine/Karras ESVO traversal and beam optimization.
Contours/filtering had previously been removed at the user's request for cubic
voxels. The later rendering-model decision replaces that requirement. Octree and
hardcoded demo sources, descriptor attachments and beam shaders are now removed.
Their camera/input regressions were retained; mesh/LOD/GPU tests replace backend tests.

The authoritative solid scene, edits, precise coordinates and saves remain intact.
See `solid-planet-world.md` for the model and `triangle-terrain-refactor.md` for the
current render/stream architecture. Earlier tests and timings are historical only.
