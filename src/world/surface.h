#pragma once
#include "scene.h"
#include "mesh.h"
#define AX_SURFACE_CELLS 16u
#define AX_SURFACE_ALL_FACES 63u
/** Coarsest sample spacing in meters that resolves procedural cave channels without biting through their roof. */
#define AX_SURFACE_CAVE_SPACING 2.0
/** Bit for the touching neighbor in direction x,y,z (each -1..1) within a sealed-neighbor mask. */
#define AX_SURFACE_NEIGHBOR(x,y,z) (1u<<((unsigned)((x)+1)+3u*(unsigned)((y)+1)+9u*(unsigned)((z)+1)))
/** Immutable surface region. Face masks connect only boundaries adjoining a coarser 2:1 neighbor. */
typedef struct {
    AxSurfaceMesh mesh;
    AxPlanetId planet;
    AxPosition origin;
    uint64_t revision;
    size_t source_triangles,visible_triangles;
    float simplification_error;
    float extent;
    uint32_t cells;
    uint8_t transition_faces;
    uint32_t sealed_neighbors;
} AxSurfaceRegion;
/** Extracts a Transvoxel-compatible indexed isosurface; replaces output only on success.
 * @param planet Read-only solid source, stable throughout extraction.
 * @param origin Planet-local cube minimum; extent is its positive side length in meters.
 * @param cells Even cell count, 2..128. transition_faces selects -X,+X,-Y,+Y,-Z,+Z stitching faces.
 * @param sealed_neighbors AX_SURFACE_NEIGHBOR mask of touching leaves too coarse to resolve caves; shared boundary
 *        samples omit channels there, so both sides agree and cave walls close instead of cracking.
 */
bool ax_surface_build(const AxPlanet *planet, const AxPosition *origin, float extent,
                      uint32_t cells, uint8_t transition_faces, uint32_t sealed_neighbors, AxSurfaceRegion *region);
/** Releases a derived mesh; accepts NULL/zero state. */
void ax_surface_destroy(AxSurfaceRegion *region);
