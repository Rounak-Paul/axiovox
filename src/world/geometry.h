#pragma once
#include "surface.h"
#define AX_WORLD_MAX_REGIONS 4096u
/** One indexed draw and its precise scene-space placement. */
typedef struct {
    AxPosition origin;
    double center[3];
    AxPlanetId planet;
    float extent;
    uint32_t first_index, index_count;
    int32_t vertex_offset;
} AxWorldRegion;
/** Immutable packed triangle geometry; density, materials and saved edits remain scene-owned. */
typedef struct {
    AxSurfaceVertex *vertices;
    uint32_t *indices;
    size_t vertex_count, index_count, count;
    AxWorldRegion *regions;
} AxWorldGeometry;
/** Packs meshes into draw ranges transactionally, retaining empty leaves for spatial coverage. */
bool ax_world_geometry_build(const AxScene *scene, const AxSurfaceRegion *regions, size_t count, AxWorldGeometry *geometry);
/** Releases packed render data; accepts NULL/zero state. */
void ax_world_geometry_destroy(AxWorldGeometry *geometry);
