#pragma once
#include "geometry.h"
#include "core/jobs.h"
#define AX_STREAM_MIN_EXTENT 8.0
/** Camera travel in meters before the layout is replanned; below the finest leaf's hysteresis band. */
#define AX_STREAM_REPLAN_DISTANCE 2.0
typedef struct AxStreamTask AxStreamTask;
/** Balanced distance-selected octree leaves with atomic geometry publication. */
typedef struct {
    AxSurfaceRegion *regions;
    AxPosition planned_eye;
    AxPlanetId planned_frame;
    size_t count, source_count;
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
