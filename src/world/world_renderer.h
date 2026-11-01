#pragma once
#include "camera.h"
#include "octree.h"
#include "renderer/renderer.h"
/** Scene render node: ray casts the sparse voxel octree on a full-screen triangle (contours per AX_WORLD_CONTOURS). */
typedef struct {
    AxRenderer *renderer;
    const AxOctree *octree;
    const AxCamera *camera;
    float lod_bias;
    float sun[3];
    uint32_t primary_steps, shadow_steps;
} AxWorldRenderer;
/**
 * Uploads the octree and registers the "world" scene node; device switches re-upload automatically.
 * @param octree,camera Borrowed; must outlive the node. The octree must not change while registered.
 */
bool ax_world_renderer_create(AxWorldRenderer *world, AxRenderer *renderer, const AxOctree *octree, const AxCamera *camera);
/** Releases the node's GPU resources after the renderer is idle; accepts zero state. Call before ax_renderer_destroy. */
void ax_world_renderer_destroy(AxWorldRenderer *world);
