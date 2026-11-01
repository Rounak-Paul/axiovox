#pragma once
#include "renderer.h"
/** Creates presentation synchronization and command resources; used by device initialization. */
bool ax_renderer_frames_create(AxRenderer *renderer);
/** Releases presentation resources after device idle; used by device shutdown. */
void ax_renderer_frames_destroy(AxRenderer *renderer);

/** Chooses a compatible color format before creating node pipelines. */
bool ax_renderer_choose_format(AxRenderer *renderer);
/** Creates/recreates presentation only; caller must own the native window's swapchain. */
bool ax_renderer_swapchain_create(AxRenderer *renderer);
/** Releases presentation only, after device idle. */
void ax_renderer_swapchain_destroy(AxRenderer *renderer);
/** Destroys device-local node states and device resources without releasing shared instance/surface/catalog. */
void ax_renderer_device_destroy(AxRenderer *renderer);
