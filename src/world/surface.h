#pragma once
#include "scene.h"
#include "mesh.h"
#define AX_SURFACE_CELLS 64u
/** Immutable surface region. Non-root regions include transition faces to the next 2:1 level. */
typedef struct {
    AxSurfaceMesh mesh;
    AxPlanetId planet;
    AxPosition origin,exclusion_origin;
    float exclusion_extent;
    uint64_t revision;
    size_t source_triangles,visible_triangles;
    float simplification_error;
    float extent;
    uint32_t cells;
    bool transitions;
} AxSurfaceRegion;
/** Extracts a Transvoxel-compatible indexed isosurface; replaces output only on success.
 * @param planet Read-only solid source, stable throughout extraction.
 * @param origin Planet-local cube minimum; extent is its positive side length in meters.
 * @param cells Even cell count, 2..128. transitions adds zero-depth 2:1 stitching faces.
 */
bool ax_surface_build(const AxPlanet *planet, const AxPosition *origin, float extent,
                      uint32_t cells, bool transitions, AxSurfaceRegion *region);
/** Releases a derived mesh; accepts NULL/zero state. */
void ax_surface_destroy(AxSurfaceRegion *region);
