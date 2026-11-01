#pragma once
#include "core/jobs.h"
#include "world_config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define AX_OCTREE_MAX_DEPTH 16
#define AX_OCTREE_NEAR_LIMIT 0x7fffu
#define AX_OCTREE_POINTER_LIMIT 0xffffffu
/**
 * Surface description sampled by the octree builder, in world units over the cube [0, extent]^3.
 * Callbacks must be pure and thread-safe; the builder calls them concurrently from worker threads.
 */
typedef struct {
    /** Signed distance (negative inside); |gradient| must not exceed lipschitz. */
    float (*distance)(const void *user, const float p[3]);
    /** ABGR surface color at a point with its outward unit normal; cell is the integer leaf voxel index. */
    uint32_t (*color)(const void *user, const float p[3], const float normal[3], const int32_t cell[3]);
    const void *user;
    float extent;
    uint32_t depth;
    float lipschitz;
} AxVoxelSource;
/**
 * Surface-only sparse voxel octree in the Laine-Karras ESVO layout, stored as one GPU-uploadable word array:
 * [descriptors: 2 words each][lookup: 1 word per descriptor][ABGR values][contours].
 *
 * Descriptor word 0: child pointer [31:17] | far [16] | valid mask [15:8] | non-leaf mask [7:0].
 * Descriptor word 1: contour pointer [31:8] | contour mask [7:0]; the pointer indexes the contour region and
 * addresses one contour per set mask bit in child-slot order.
 * Child pointers are relative to the descriptor; with far set they address a far slot whose word 0 holds the
 * full offset relative to the same descriptor. Lookup entries are value pointer [31:8] | value mask [7:0].
 *
 * Contour (paper Fig. 4): thickness [31:25] unsigned | position [24:18] | nx [17:12] | ny [11:6] | nz [5:0] signed.
 * For a voxel of side s and center c it bounds the slab |n.(x - c) - pos| <= thick with n = bits / 32,
 * pos = bits * 1.5s / 64 and thick = bits * 0.75s / 64. Every voxel's cube-slab intersection is enclosed by all
 * ancestor slabs, so ancestor contours never remove descendant surface.
 * Contours are fitted only when AX_WORLD_CONTOURS is non-zero (CMake option AXIOVOX_WORLD_CONTOURS); otherwise every
 * contour mask is zero and voxels are plain cubes.
 */
typedef struct {
    uint32_t *words;
    size_t word_count;
    uint32_t descriptor_count, lookup_offset, value_offset, contour_offset;
    uint32_t value_count, contour_count, far_count;
    uint32_t size, depth;
    float extent;
} AxOctree;
/**
 * Builds the octree by sampling a surface down to 2^depth voxels per side.
 * @param jobs Optional worker pool for parallel subtree construction; NULL builds on the calling thread.
 *             Must be called from the pool's owner thread; it waits for all accepted work.
 * @return false on invalid input, allocation failure, or a layout exceeding the 24/32-bit addressing limits.
 */
bool ax_octree_build(AxOctree *octree, const AxVoxelSource *source, AxJobs *jobs);
/** Releases octree storage; accepts zero state. */
void ax_octree_destroy(AxOctree *octree);
/**
 * Point query decoding descriptors and contours exactly as the GPU does.
 * @param p World position.
 * @param color Receives the leaf ABGR color when non-NULL.
 * @return true when p lies inside a leaf voxel and within every contour slab on its path.
 */
bool ax_octree_sample(const AxOctree *octree, const float p[3], uint32_t *color);
