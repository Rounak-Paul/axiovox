#pragma once
#include "camera.h"
#include "geometry.h"
#include "renderer/renderer.h"
typedef enum { AX_WORLD_VIEW_DEFAULT, AX_WORLD_VIEW_WIREFRAME, AX_WORLD_VIEW_NORMALS } AxWorldView;
/** Triangle terrain node; scene volumes remain GPU-independent and geometry is borrowed until replaced. */
typedef struct {
    AxRenderer *renderer;
    const AxCamera *camera;
    const AxWorldGeometry *geometry;
    AxPosition view_origin;
    double frame_center[3];
    float local_up[3], altitude, atmosphere_height, sun[3];
    bool shadows_enabled;
    AxWorldView view;
    float fade;
} AxWorldRenderer;
/** Uploads triangle geometry and registers the world node; camera and geometry must outlive registration. */
bool ax_world_renderer_create(AxWorldRenderer *world, AxRenderer *renderer, const AxWorldGeometry *geometry, const AxCamera *camera);
/** Publishes replacement geometry between frames, shown at once; keeps the current asset if preparation fails. */
bool ax_world_renderer_set_geometry(AxWorldRenderer *world, const AxWorldGeometry *geometry);
/** Publishes replacement geometry that blends in over the outgoing layout, hiding LOD changes; debug views switch at once.
 * world->fade is the outgoing layout's weight, 1 to 0. Publishing during a fade snaps it, so wait for zero first.
 */
bool ax_world_renderer_fade_geometry(AxWorldRenderer *world, const AxWorldGeometry *geometry);
/** Advances an active fade by dt seconds; long stalls are clamped so a slow frame cannot skip the blend. */
void ax_world_renderer_advance(AxWorldRenderer *world, double dt);
/** Drains/releases device resources; accepts NULL/zero state. Call before destroying the renderer. */
void ax_world_renderer_destroy(AxWorldRenderer *world);

/** Selects the default, depth-tested wireframe or normal view between frames. */
bool ax_world_renderer_set_view(AxWorldRenderer *world, AxWorldView view);
