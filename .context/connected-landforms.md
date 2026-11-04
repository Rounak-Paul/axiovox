# Connected landforms

## Scope
Implement the proposed mountain-to-ocean hierarchy: connected main ridges and spurs, valley corridors and passes, foothills, terraces, coastal cliffs/shelves, and surface colors that describe the landforms. Preserve the calmer water and the preceding latitude/coastline work. No commits/pushes.

## Owners
- `src/world/terrain.c::continental_height` owns the continental recipe. Broad continent/coast masks determine shelves and interior lowlands. A warped regional fault field determines main ridge crests and selected continental-margin ranges. Erosion-inspired branches sample the same regional domain. A shared drainage-shaped mask opens valley floors and saddles; foothills and small detail fade on those floors.
- Shelf/coast cross-sections rise from deep ocean through a shallow shelf to lowlands. Selected dry upland valleys receive continuous terraces. Regional fault/valley masks select raised coastal headlands/cliffs instead of applying them everywhere.
- `AxTerrainClimate` carries normalized valley/cover masks into `scene.c`. Valleys retain more moisture, grass albedo varies with cover, exposed slopes receive rock, and steep faces retain less snow. Beaches use a bounded 30 m elevation band rather than painting all lowlands sand. These are procedural appearances; no vegetation instances or simulated drainage were added.
- Density still evaluates one radial scalar field with bounded height, and saved recipes remain authoritative. Moon/wave geometry and the water shader are unchanged in this task.

## Representative region
Seed 42, radius 1,000,000 m, relief budget 10,000 m, frequency .005. Radial center (-.15211999,.05957031,-.9865652); tangent patch spans 120 km. A 64x64 survey with 25 m slope offsets found -682..8329 m elevation, 171 valley-floor samples (valley >.99) with mean tangent slope .181, and 1932 exposed ridge samples with mean .649. There were 218 shallow shelf samples (-400..-5 m). These describe this fixture, not the whole planet.

## Validation
Initial Release surface/stream/procedural checks pass. All 14 CTests pass after valley/cliff tuning (31.08 s). A new representative-region regression verifies relief, shallow shelf coverage, climate-mask bounds and valley floors gentler than exposed ridges. The representative-region regression passes. Final Release build, all 14 CTests, ground/orbital/shoreline diagnostic captures, default-world smoke run and git diff --check pass. ASan/UBSan procedural and scene tests also pass. Arrival selection now rejects steep immediate foregrounds and penalizes grade when choosing a mountain-facing site.

## Visuals
`.context/previews/earthlike-{mountains,globe,shore}.png` use actual Vulkan terrain/water shaders on diagnostic sampled meshes. Temporary capture sources live in `/tmp/axiovox-{visual,globe,shore}.c`; `/tmp/render-final-previews.py` relinks them against current Release libraries. These captures check recipe/material appearance, not live-stream coverage or performance. The full-world stream is verified separately by tests and smoke runs.
