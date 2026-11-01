#pragma once
#include "mesh.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Measured source/reduced triangle counts and the simplifier's absolute error estimate in meters. */
typedef struct { size_t source_triangles,visible_triangles,triangles; float error; } AxMeshStats;
/** Reduces triangles under a 0.15*cell_size simplifier error metric, then optimizes cache/fetch order transactionally.
 * extent defines region-local [0,extent]^3; exclusion optionally supplies aligned inner cube xyz/extent.
 * Covered triangles are removed; active inner/outer borders and material seams stay locked.
 * Called on newly extracted streaming regions; max reduction is constrained by topology and seam ownership.
 */
bool ax_mesh_optimize(AxSurfaceMesh *mesh,float extent,float cell_size,const float exclusion[4],AxMeshStats *stats);
#ifdef __cplusplus
}
#endif
