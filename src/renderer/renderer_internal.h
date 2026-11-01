#pragma once
#include "renderer.h"
/** Creates presentation synchronization and command resources; used by device initialization. */
bool ax_renderer_frames_create(AxRenderer *renderer);
/** Releases presentation resources after device idle; used by device shutdown. */
void ax_renderer_frames_destroy(AxRenderer *renderer);
