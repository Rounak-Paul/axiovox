#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/** Indexed surface vertex in region-local meters; color is packed ABGR. */
typedef struct { float position[3], normal[3]; uint32_t color; } AxSurfaceVertex;
/** Disposable indexed mesh, independent of the authoritative volume. */
typedef struct {
    AxSurfaceVertex *vertices;
    uint32_t *indices;
    size_t vertex_count, index_count;
} AxSurfaceMesh;
