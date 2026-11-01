#pragma once
#include "camera.h"
#include "geometry.h"
#include "renderer/renderer.h"
/** Triangle terrain node; scene volumes remain GPU-independent and geometry is borrowed until replaced. */
typedef struct {
    AxRenderer *renderer;
    const AxCamera *camera;
    const AxWorldGeometry *geometry;
    AxPosition view_origin;
    double frame_center[3];
    float local_up[3], altitude, atmosphere_height, sun[3];
} AxWorldRenderer;
/** Uploads triangle geometry and registers the world node; camera and geometry must outlive registration. */
bool ax_world_renderer_create(AxWorldRenderer *world, AxRenderer *renderer, const AxWorldGeometry *geometry, const AxCamera *camera);
/** Publishes replacement geometry between frames; keeps the current asset if preparation fails. */
bool ax_world_renderer_set_geometry(AxWorldRenderer *world, const AxWorldGeometry *geometry);
/** Drains/releases device resources; accepts NULL/zero state. Call before destroying the renderer. */
void ax_world_renderer_destroy(AxWorldRenderer *world);
