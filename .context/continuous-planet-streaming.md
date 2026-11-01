# Continuous planetary scene

Current backend and validation: `triangle-terrain-refactor.md`.
World model and persistence: `solid-planet-world.md`.

User requires one continuous scene from surface to space, like Google Earth, without
separate globe models or scene switching. Terra and its moon Luna remain in shared
space. Camera-relative GPU rendering and precise planet-local positions preserve
movement precision. Detail follows the nearest surface without changing camera pose.

The original octree-forest streamer has been replaced by coherent triangle-mesh LOD
layouts. Worker snapshots isolate edits and loading from in-flight generation. Old
coverage remains until complete new layouts publish. Whole-body coverage and local
terrain use the same density volume and same triangle extraction path.

Flight: mouse look, W/S axial, A/D strafe, E/Q fixed Y, Shift altitude-scaled cruise.
world.speed sets base speed; world.altitude positions radially for inspection;
world.visit is an optional navigation convenience. Moon position is currently static.
