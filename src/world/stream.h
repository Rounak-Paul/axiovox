#pragma once
#include "geometry.h"
#include "core/jobs.h"
#define AX_STREAM_DETAIL_LEVELS 40u
#define AX_STREAM_BASE_EXTENT 32.0
typedef struct AxStreamTask AxStreamTask;
/** Coherent nested surface meshes with complete whole-body coverage. Only one immutable layout job runs at once. */
typedef struct {
    AxSurfaceRegion regions[AX_WORLD_MAX_REGIONS];
    size_t count;
    uint64_t epoch;
    AxStreamTask *task;
    bool dirty, failed;
} AxWorldStream;
/** Builds initial coverage before presentation. jobs is the borrowed owner-thread pool, optionally NULL. */
bool ax_world_stream_create(AxWorldStream *stream, const AxScene *scene, AxPlanetId planet,
                            const AxPosition *eye, AxJobs *jobs);
/** Schedules immutable layout updates and commits matching scene revisions atomically; old coverage survives failures. */
bool ax_world_stream_update(AxWorldStream *stream, const AxScene *scene, AxPlanetId frame,
                            const AxPosition *eye, AxJobs *jobs);
/** Drains accepted work before destruction. jobs must be the same pool used by update while a task exists. */
void ax_world_stream_destroy(AxWorldStream *stream, AxJobs *jobs);
