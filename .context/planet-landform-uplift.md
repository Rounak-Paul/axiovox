# Planet and moon landform uplift

## Request
Make Terra and Luna more interesting terrain to explore.

## Implementation
- `src/world/terrain.c`: Terra retains warped continental basins but adds stronger mountain provinces, ridge networks, valley corridors and foothills. Local landform frequencies use a 100 km minimum reference radius so small test/custom bodies do not inherit unresolved sub-meter mountains.
- Luna replaces nearest-cell bowls with sparse seeded compact impacts, variable radii, continuous raised rims, flattened bowls and central peaks in larger impacts. Three scales sit over broad basins and rough highlands. Impact support fits within neighboring lattice cells; each layer is bounded to preserve conservative density bounds.
- `src/world/scene.c`: cratered rock surfaces blend darker low terrain and brighter highlands/rim slopes. Explicit edited materials retain precedence.
- Existing continental/cratered recipe identities now evaluate the updated geometry, including loaded saves. Wave geometry is unchanged. No simulated erosion, collision, or orbital changes. Water is documented separately in `spherical-ocean-rendering.md`.

## Validation
Release build passes. Procedural tests cover local 2 km traverses on both recipes, height bounds and millimeter continuity alongside existing spherical/density regressions. Surface tests pass after reducing initial local frequencies that exceeded coarse mesh error tolerances. Final streaming checks and all 14 Release CTests pass (24.52 s); git diff --check passes. No interactive appearance review performed; visual quality remains subjective.

## Mountain layering follow-up
The former positive ridged octave average saturated its smooth mask and made broad rounded uplands. Signed fBm is now composed first, then remapped around its zero contours into narrow cubed crests. The existing detail field modulates spurs; the valley field cuts saddles through the ridges. Province masks separate mountains from broad plateau shoulders and gentler plains. No extra noise samples or octaves were added. Small-body province sampling shares the minimum physical reference scale. Bounded layer weights retain the +/- relief contract. Planetary regression now requires peaks above 3 km and more than 4 km total relief for seed 42; coarse surface and streaming checks pass.

## Earth-like rebuild (supersedes mountain follow-up above)
User rejected the isolated sharp walls and requested natural large ranges and an Earth-like orbital appearance. Studied https://www.decarpentier.nl/scape-procedural-extensions (gradient-guided octave distortion and valley detail suppression) and https://www.redblobgames.com/maps/terrain-from-noise/ (separate scales and octave weighting).

`ax_noise_erosion3` in shared math composes bounded ridges with coarse analytic gradients steering finer octaves and amplitude fading in valleys. Terrain uses broad warped mountain belts with a continuous raised foundation, plus smaller ridges, foothills and plains. Continental coastline masks are separate from interior mountains; mountains no longer become islands just by exceeding sea level. Latitude, elevation and broad subtropical dryness control snow/desert/rock distribution. Arrival searches dry foothills facing taller terrain rather than a flat height-only target.

New fields exposed micrometer-level discrepancies in shared float vertex positions, straddling the seam test's millimeter rounding bins. `surface.c` now uses a canonical 16-bit interpolation fraction on each edge: dyadic stream chunk translations then preserve exactly representable shared positions (also across 2:1 transition edges). Error is bounded to edge length / 131072. No relaxation of seam tests.

Visual diagnostics: actual height samples plotted over a 120 km province, and actual Vulkan terrain/water shaders rendered with diagnostic heightfield/globe meshes in `/tmp/axiovox-mountain-study.png` and `.context/previews/earthlike-{mountains,globe,shore}.png`. These are not streaming-performance evidence or user appearance approval. Final validation recorded in spherical-ocean-rendering.md.

## Exaggerated exploration scale
User requested taller mountains and calmer, larger water features. Default Terra relief budget is now 10,000 m (was 6,500 m); mountain wavelength increases from radius/55 to radius/40. Lowland layers receive a 0.65 factor so the higher mountain budget does not inflate plains and ocean basins by the same amount. Existing saved relief values remain respected. Mountain masks/continents and sampling cost are unchanged. Final sampled Vulkan previews refreshed under `.context/previews/`.

## Coastline breakup and latitude-directed continents
User requested rougher shores and less scattered land concentrated nearer the equator. Large continental fBm now uses three octaves and .38 gain to favor coherent land masses. A smooth latitude bias (+.045 - .16 * latitude squared) favors low/mid-latitude continents without imposing a hard polar boundary. This is art direction, not a rotation/tectonic simulation; geological background: https://www.usgs.gov/educational-resources/plate-tectonics.

Coast-only noise layers (radius/28 and radius/160, three/two octaves) perturb the land mask near its shoreline, forming bays/headlands and smaller inlets. Mountain interiors use the original broad continent value, so coastal detail cannot scatter alpine features over the oceans. Seed-42 area-uniform survey: 992/2048 land samples, 580/1024 low-latitude land and 204/512 polar land. Added a global coverage/latitude-bias regression. Geometry bounds and saved recipe semantics remain intact. Previews refreshed under `.context/previews/`; final validation recorded in water context.

## Connected mountain-to-ocean structure
The subsequent structural refactor and current validation live in `connected-landforms.md`; it supersedes the independent mountain/valley composition above.
