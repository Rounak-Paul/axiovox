#include "stream.h"
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
struct AxStreamTask {
    AxScene snapshot;
    AxSurfaceRegion previous[AX_WORLD_MAX_REGIONS], next[AX_WORLD_MAX_REGIONS];
    bool reused[AX_WORLD_MAX_REGIONS];
    size_t count;
    atomic_bool complete;
    bool success;
};
/** Selects a nearby surface without changing the camera's planet reference frame. */
static AxPlanetId nearest_planet(const AxScene *scene, AxPlanetId frame, const AxPosition *eye, AxPosition *local)
{
    const AxPlanet *anchor=ax_scene_planet(scene,frame);
    if (!anchor) return 0;
    const AxPlanetDesc *a=ax_planet_description(anchor);
    double closest=INFINITY;
    AxPlanetId selected=0;
    for (size_t i=0; i<scene->count; ++i) {
        const AxPlanetDesc *p=ax_planet_description(scene->planets[i]);
        AxPosition position=*eye;
        double shift[3];
        for (size_t k=0; k<3; ++k) shift[k]=a->center[k]-p->center[k];
        if (!ax_position_move(&position,shift)) continue;
        const AxPosition zero={0};
        double delta[3];
        ax_position_difference(&position,&zero,delta);
        double altitude=fabs(hypot(hypot(delta[0],delta[1]),delta[2])-p->radius);
        if (altitude<closest) { closest=altitude; selected=p->id; *local=position; }
    }
    return selected;
}
/** Snaps nested cubes to a shared power-of-two grid, preserving 2:1 transition-plane alignment. */
static bool detail_origin(const AxPosition *eye, double extent, AxPosition *origin)
{
    *origin=*eye;
    if (!ax_position_move(origin,(double[3]){-extent/2,-extent/2,-extent/2})) return false;
    int64_t chunks=(int64_t)(extent/8/AX_CHUNK_METERS);
    for (size_t i=0; i<3; ++i) {
        int64_t remainder=origin->chunk[i]%chunks;
        if (remainder<0) remainder+=chunks;
        if (origin->chunk[i]<INT64_MIN+remainder) return false;
        origin->chunk[i]-=remainder; origin->offset[i]=0;
    }
    return true;
}
/** Checks precise normalized origins without depending on struct padding. */
static bool same_origin(const AxPosition *a, const AxPosition *b)
{
    for (size_t i=0; i<3; ++i) if (a->chunk[i]!=b->chunk[i] || a->offset[i]!=b->offset[i]) return false;
    return true;
}
/** Plans an entire consistent layout and identifies immutable meshes reusable from the live layout. */
static bool plan(AxStreamTask *task, const AxWorldStream *stream, const AxScene *scene, AxPlanetId frame, const AxPosition *eye)
{
    AxPosition local;
    AxPlanetId nearest=nearest_planet(scene,frame,eye,&local);
    const AxPlanet *target=ax_scene_planet(scene,nearest);
    if (!target || scene->count+AX_STREAM_DETAIL_LEVELS>AX_WORLD_MAX_REGIONS) return false;
    double target_extent=ax_planet_extent(target);
    double maximum_extent=AX_STREAM_BASE_EXTENT*exp2(AX_STREAM_DETAIL_LEVELS);
    task->count=scene->count+AX_STREAM_DETAIL_LEVELS;
    for (size_t i=0; i<task->count; ++i) {
        bool root=i>=AX_STREAM_DETAIL_LEVELS;
        const AxPlanet *planet=root ? scene->planets[i-AX_STREAM_DETAIL_LEVELS] : target;
        double extent=root ? ax_planet_extent(planet) : AX_STREAM_BASE_EXTENT*exp2((double)i);
        if (root && extent>maximum_extent) return false;
        if (!root && extent>=target_extent) continue;
        AxSurfaceRegion *out=&task->next[i];
        *out=(AxSurfaceRegion){.planet=ax_planet_description(planet)->id,.revision=ax_planet_revision(planet),
            .extent=(float)extent,.cells=AX_SURFACE_CELLS,.transitions=!root};
        if (root) {
            if (!ax_position_from_meters(&out->origin,(double[3]){-extent/2,-extent/2,-extent/2})) return false;
        } else if (!detail_origin(&local,extent,&out->origin)) return false;
        if (i<stream->count) {
            const AxSurfaceRegion *old=&stream->regions[i];
            if (old->planet==out->planet && old->revision==out->revision && old->extent==out->extent && same_origin(&old->origin,&out->origin)) {
                task->reused[i]=true; task->previous[i]=*old;
            }
        }
    }
    return true;
}
/** Builds only changed meshes from a private scene snapshot; never waits on the worker pool. */
static void build_layout(void *user)
{
    AxStreamTask *task=user;
    task->success=true;
    for (size_t i=0; i<task->count; ++i) {
        AxSurfaceRegion *out=&task->next[i];
        if (task->reused[i]) { *out=task->previous[i]; continue; }
        if (out->extent<=0) continue;
        AxSurfaceRegion spec=*out;
        if (!ax_surface_build(ax_scene_planet(&task->snapshot,spec.planet),&spec.origin,spec.extent,
                              spec.cells,spec.transitions,out)) { task->success=false; break; }
    }
    atomic_store_explicit(&task->complete,true,memory_order_release);
}
/** Releases task-owned meshes without touching borrowed live meshes. */
static void destroy_task(AxStreamTask *task)
{
    if (!task) return;
    for (size_t i=0; i<task->count; ++i) if (!task->reused[i]) ax_surface_destroy(&task->next[i]);
    ax_scene_destroy(&task->snapshot); free(task);
}
/** Transfers a completed coherent layout, leaving worker cleanup without any owned mesh pointers. */
static void commit(AxWorldStream *stream, AxStreamTask *task)
{
    for (size_t i=0; i<stream->count; ++i) if (!task->reused[i]) ax_surface_destroy(&stream->regions[i]);
    memcpy(stream->regions,task->next,sizeof(stream->regions));
    memset(task->next,0,sizeof(task->next));
    stream->count=task->count; stream->dirty=true;
}
bool ax_world_stream_create(AxWorldStream *stream, const AxScene *scene, AxPlanetId planet, const AxPosition *eye, AxJobs *jobs)
{
    (void)jobs;
    if (!stream || stream->count || stream->task || !scene || !scene->count || !eye) return false;
    AxStreamTask *task=calloc(1,sizeof(*task));
    if (!task) return false;
    atomic_init(&task->complete,false);
    bool success=plan(task,stream,scene,planet,eye) && ax_scene_clone(scene,&task->snapshot);
    if (success) { build_layout(task); success=task->success; }
    if (success) commit(stream,task);
    destroy_task(task);
    return success;
}
bool ax_world_stream_update(AxWorldStream *stream, const AxScene *scene, AxPlanetId frame, const AxPosition *eye, AxJobs *jobs)
{
    if (!stream || !scene || !eye || !jobs || !stream->count) return false;
    if (stream->task) {
        AxStreamTask *task=stream->task;
        if (!atomic_load_explicit(&task->complete,memory_order_acquire)) return true;
        bool current=task->success && scene->count==task->snapshot.count;
        for (size_t i=0; current && i<task->snapshot.count; ++i) {
            const AxPlanet *snapshot=task->snapshot.planets[i];
            const AxPlanet *live=ax_scene_planet(scene,ax_planet_description(snapshot)->id);
            current=live && ax_planet_revision(live)==ax_planet_revision(snapshot);
        }
        if (current) commit(stream,task);
        bool success=task->success;
        destroy_task(task); stream->task=NULL;
        if (!success) return false;
    }
    AxStreamTask *task=calloc(1,sizeof(*task));
    if (!task) return false;
    atomic_init(&task->complete,false);
    if (!plan(task,stream,scene,frame,eye)) { destroy_task(task); return false; }
    bool changed=task->count!=stream->count;
    for (size_t i=0; i<task->count; ++i)
        if ((task->next[i].extent>0 && !task->reused[i]) || (task->next[i].extent<=0 && stream->regions[i].extent>0)) changed=true;
    if (!changed) { destroy_task(task); return true; }
    if (!ax_scene_clone(scene,&task->snapshot) || !ax_jobs_submit(jobs,build_layout,task)) { destroy_task(task); return false; }
    stream->task=task;
    return true;
}
void ax_world_stream_destroy(AxWorldStream *stream, AxJobs *jobs)
{
    if (!stream) return;
    if (stream->task) {
        if (!jobs) return;
        ax_jobs_wait(jobs);
    }
    destroy_task(stream->task);
    for (size_t i=0; i<stream->count; ++i) ax_surface_destroy(&stream->regions[i]);
    *stream=(AxWorldStream){0};
}
