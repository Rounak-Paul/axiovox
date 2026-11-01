#pragma once
#include "geometry.h"
#include "core/jobs.h"
#define AX_STREAM_MIN_EXTENT 8.0
/** Camera travel in meters before the layout is replanned; below the finest leaf's hysteresis band. */
#define AX_STREAM_REPLAN_DISTANCE 2.0
/** Leaf budget of the first layout, small enough to build before the first frame; later layouts quadruple it. */
#define AX_STREAM_BOOTSTRAP_LEAVES 64u
/** Camera travel in meters since the last full layout at which the nearby terrain is published first. */
#define AX_STREAM_FOCUS_TRAVEL 32.0
/** Radius in meters around the camera that a nearby-first layout follows; farther chunks keep their structure. */
#define AX_STREAM_FOCUS_RADIUS 64.0
/** Finest leaf extent a nearby-first layout refines to; the full layout that follows adds anything finer. */
#define AX_STREAM_FOCUS_MIN_EXTENT 16.0
typedef struct AxStreamTask AxStreamTask;
/** Balanced distance-selected octree leaves with atomic geometry publication. */
typedef struct {
    AxSurfaceRegion *regions;
    AxPosition planned_eye, full_eye;
    AxPlanetId planned_frame;
    size_t count, source_count, finished_budget;
    double focus_extent;
    uint64_t epoch;
    AxStreamTask *task;
    bool dirty, failed, focused;
} AxWorldStream;
/** Builds coarse camera-prioritized coverage before presentation; updates then refine it in the background.
 * jobs is the borrowed owner-thread pool, optionally NULL.
 */
bool ax_world_stream_create(AxWorldStream *stream, const AxScene *scene, AxPlanetId planet,
                            const AxPosition *eye, AxJobs *jobs);
/** Schedules immutable layout updates and commits matching scene revisions atomically; old coverage survives failures. */
bool ax_world_stream_update(AxWorldStream *stream, const AxScene *scene, AxPlanetId frame,
                            const AxPosition *eye, AxJobs *jobs);
/** Reports whether a layout is being built, or none has yet finished at the full leaf budget. */
bool ax_world_stream_refining(const AxWorldStream *stream);
/** Drains accepted work before destruction. jobs must be the same pool used by update while a task exists. */
void ax_world_stream_destroy(AxWorldStream *stream, AxJobs *jobs);
