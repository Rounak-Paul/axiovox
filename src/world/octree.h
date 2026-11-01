#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define AX_OCTREE_MAX_DEPTH 16
#define AX_OCTREE_NEAR_LIMIT 0x7fffu
/**
 * Dense build input: cubic grid of palette indices, index 0 is empty.
 * Palette colors are ABGR (red in the low byte).
 */
typedef struct {
    uint32_t size;
    uint8_t *cells;
    uint32_t palette[256];
} AxVoxelGrid;
/**
 * Sparse voxel octree in the Laine-Karras ESVO layout, stored as one GPU-uploadable word array.
 *
 * Child descriptor (2 words, one per non-leaf voxel, children of a voxel are consecutive):
 *   word 0: child pointer [31:17] | far [16] | valid mask [15:8] | non-leaf mask [7:0]
 *   word 1: contour pointer [31:8] | contour mask [7:0] (reserved, zero)
 * The child pointer is relative to the descriptor; when far is set it addresses a far slot whose
 * word 0 holds the full offset relative to the same descriptor.
 *
 * Attachment lookup (1 word per descriptor slot): value pointer [31:8] | value mask [7:0].
 * Values are ABGR colors, one per set mask bit in child-slot order; non-leaf children carry
 * averaged colors for level-of-detail termination.
 */
typedef struct {
    uint32_t *words;
    size_t word_count;
    uint32_t descriptor_count, lookup_offset, value_offset, value_count, far_count;
    uint32_t size, depth;
} AxOctree;
/**
 * Allocates an empty grid.
 * @param size Power-of-two edge length from 2 through 2^AX_OCTREE_MAX_DEPTH.
 */
bool ax_voxel_grid_create(AxVoxelGrid *grid, uint32_t size);
/** Releases grid cells; accepts zero state. */
void ax_voxel_grid_destroy(AxVoxelGrid *grid);
/** Writes one palette index; out-of-bounds writes are ignored. */
void ax_voxel_grid_set(AxVoxelGrid *grid, int64_t x, int64_t y, int64_t z, uint8_t material);
/** Reads one palette index; out-of-bounds reads return 0. */
uint8_t ax_voxel_grid_get(const AxVoxelGrid *grid, int64_t x, int64_t y, int64_t z);
/**
 * Builds the octree from a grid. Uniformly colored solid regions collapse into coarser leaves.
 * @return false on invalid input, allocation failure, or a layout exceeding the 32-bit addressing limits.
 */
bool ax_octree_build(AxOctree *octree, const AxVoxelGrid *grid);
/** Releases octree storage; accepts zero state. */
void ax_octree_destroy(AxOctree *octree);
/**
 * Point query by traversing the encoded descriptors, exactly as the GPU does.
 * @param color Receives the leaf ABGR color when non-NULL.
 * @return true when the cell lies inside the octree and is solid.
 */
bool ax_octree_sample(const AxOctree *octree, int64_t x, int64_t y, int64_t z, uint32_t *color);
