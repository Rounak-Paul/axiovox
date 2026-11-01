#include "stream.h"
#include "mesh_optimizer.h"
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
typedef struct { struct AxStreamTask *task; size_t slot; } MeshWork;
typedef struct { AxSurfaceRegion spec; double minimum[3],eye[3],score,root_extent; bool uniform,settled; } Leaf;
/** Reports whether a leaf's own lattice spacing resolves procedural caves. */
static bool resolves_caves(float extent)
{
    return (double)extent/AX_SURFACE_CELLS<=AX_SURFACE_CAVE_SPACING;
}
struct AxStreamTask {
    AxScene snapshot;
    AxWorldStream previous;
    bool planning_submitted;
    atomic_bool planned;
    AxSurfaceRegion *next;
    size_t *borrowed;
    MeshWork *work;
    size_t count,work_count,submitted;
    AxPosition eye;
    AxPlanetId frame;
    atomic_size_t remaining;
    atomic_bool success;
};
/** Compares normalized origins without struct padding. */
static bool same_origin(const AxPosition *a,const AxPosition *b)
{
    for (size_t k=0;k<3;++k) if (a->chunk[k]!=b->chunk[k] || a->offset[k]!=b->offset[k]) return false;
    return true;
}
#define REFINED_SIZE (AX_WORLD_MAX_REGIONS*8u)
_Static_assert((REFINED_SIZE&(REFINED_SIZE-1))==0,"Refined-cell set requires a power-of-two capacity");
/** Hashes one octree cell address; zero is reserved for unused set slots. A collision only widens hysteresis for that cell. */
static uint64_t cell_key(AxPlanetId planet,double extent,const double minimum[3],double root_extent)
{
    uint64_t h=planet^((uint64_t)ilogb(extent)<<56);
    for (size_t k=0;k<3;++k) {
        h^=(uint64_t)llround((minimum[k]+root_extent*0.5)/extent)+UINT64_C(0x9e3779b97f4a7c15)+(h<<6)+(h>>2);
        h^=h>>30; h*=UINT64_C(0xbf58476d1ce4e5b9);
    }
    h^=h>>27;
    return h ? h : 1;
}
/** Finds a key's slot in the refined-cell set: its existing entry, or the empty slot that would hold it. */
static uint64_t *refined_slot(uint64_t *refined,uint64_t key)
{
    size_t slot=(size_t)key&(REFINED_SIZE-1);
    while (refined[slot] && refined[slot]!=key) slot=(slot+1)&(REFINED_SIZE-1);
    return &refined[slot];
}
/** Records every proper ancestor of the published leaves, so scoring can apply merge hysteresis in constant time.
 * Each walk stops at a known ancestor; stops inserting at half capacity, which only narrows hysteresis. Returns NULL on allocation failure.
 */
static uint64_t *refined_cells(const AxWorldStream *stream,const AxScene *scene)
{
    uint64_t *refined=calloc(REFINED_SIZE,sizeof(*refined));
    if (!refined) return NULL;
    const AxPosition zero={0}; size_t used=0;
    for (size_t i=0;i<stream->count;++i) {
        const AxSurfaceRegion *old=&stream->regions[i];
        const AxPlanet *planet=ax_scene_planet(scene,old->planet);
        if (!planet) continue;
        double root=fmax(ax_planet_extent(planet),AX_STREAM_MIN_EXTENT),origin[3];
        ax_position_difference(&old->origin,&zero,origin);
        for (double extent=(double)old->extent*2;extent<=root && used<REFINED_SIZE/2;extent*=2) {
            double minimum[3];
            for (size_t k=0;k<3;++k) minimum[k]=floor((origin[k]+root*0.5)/extent)*extent-root*0.5;
            uint64_t key=cell_key(old->planet,extent,minimum,root),*slot=refined_slot(refined,key);
            if (*slot) break;
            *slot=key; ++used;
        }
    }
    return refined;
}
/** Scores a fixed cube by Euclidean camera-to-box distance.
 * Uniform proves an empty mesh at this leaf's own resolution; settled estimates that refining it would add nothing.
 * Coarse leaves still refine toward possible caves, but cave-only leaves stop once their channels are resolved.
 * Leaves larger than the body radius always refine, so distant bodies keep a round silhouette.
 */
static void score_leaf(Leaf *leaf,const AxScene *scene,uint64_t *refined)
{
    const AxPlanet *planet=ax_scene_planet(scene,leaf->spec.planet);
    const AxPosition zero={0};
    ax_position_difference(&leaf->spec.origin,&zero,leaf->minimum);
    double low,high;
    bool caves=resolves_caves(leaf->spec.extent);
    leaf->uniform=ax_planet_density_bounds(planet,&leaf->spec.origin,leaf->spec.extent,caves,&low,&high) && (low>0 || high<0);
    leaf->settled=!ax_planet_surface_likely(planet,&leaf->spec.origin,leaf->spec.extent,!caves);
    double distance=0;
    for (size_t k=0;k<3;++k) {
        double d=fmax(leaf->minimum[k]-leaf->eye[k],leaf->eye[k]-leaf->minimum[k]-leaf->spec.extent);
        if (d>0) distance+=d*d;
    }
    double threshold=*refined_slot(refined,cell_key(leaf->spec.planet,leaf->spec.extent,leaf->minimum,leaf->root_extent)) ? 1.5 : 1.25;
    leaf->score=leaf->settled || leaf->spec.extent<=AX_STREAM_MIN_EXTENT ? 0 : threshold*leaf->spec.extent/fmax(sqrt(distance),0.001);
    if (!leaf->settled && leaf->spec.extent>AX_STREAM_MIN_EXTENT && leaf->spec.extent>ax_planet_description(planet)->radius) leaf->score=INFINITY;
}
typedef struct { size_t slot; Leaf leaf; } Undo;
#define LOOKUP_SIZE (AX_WORLD_MAX_REGIONS*4u)
_Static_assert((LOOKUP_SIZE&(LOOKUP_SIZE-1))==0,"Hierarchy lookup requires a power-of-two capacity");
typedef struct { AxPosition origin; AxPlanetId planet; float extent; size_t index; } LeafEntry;
/** Hashes fixed lattice addresses into a bounded open-addressed hierarchy. */
static size_t leaf_hash(const AxSurfaceRegion *spec)
{
    uint64_t h=spec->planet^(uint64_t)ilogbf(spec->extent);
    for (size_t k=0;k<3;++k) { h^=(uint64_t)spec->origin.chunk[k]+UINT64_C(0x9e3779b97f4a7c15)+(h<<6)+(h>>2); }
    h^=h>>30; h*=UINT64_C(0xbf58476d1ce4e5b9); h^=h>>27;
    return (size_t)h&(LOOKUP_SIZE-1);
}
/** Finds an existing hierarchy address or its insertion slot; planet zero marks unused entries. */
static LeafEntry *lookup(LeafEntry *table,const AxSurfaceRegion *spec)
{
    size_t slot=leaf_hash(spec);
    for (size_t i=0;i<LOOKUP_SIZE;++i,slot=(slot+1)&(LOOKUP_SIZE-1)) {
        LeafEntry *entry=&table[slot];
        if (!entry->planet || (entry->planet==spec->planet && entry->extent==spec->extent && same_origin(&entry->origin,&spec->origin))) return entry;
    }
    return NULL;
}
/** Looks up a coarser leaf containing a probe just beyond a face, edge or corner. */
static size_t coarser_neighbor(LeafEntry *table,const Leaf *leaf,int x,int y,int z)
{
    int direction[3]={x,y,z}; double point[3];
    for (size_t k=0;k<3;++k) {
        point[k]=leaf->minimum[k]+leaf->spec.extent*0.5+direction[k]*(leaf->spec.extent*0.5+1);
        if (point[k]<-leaf->root_extent*0.5 || point[k]>=leaf->root_extent*0.5) return SIZE_MAX;
    }
    AxSurfaceRegion key={.planet=leaf->spec.planet};
    for (double extent=leaf->spec.extent*2;extent<=leaf->root_extent;extent*=2) {
        double minimum[3];
        for (size_t k=0;k<3;++k) minimum[k]=floor((point[k]+leaf->root_extent*0.5)/extent)*extent-leaf->root_extent*0.5;
        key.extent=(float)extent;
        if (!ax_position_from_meters(&key.origin,minimum)) return SIZE_MAX;
        LeafEntry *entry=lookup(table,&key);
        if (entry && entry->planet && entry->index!=SIZE_MAX) return entry->index;
    }
    return SIZE_MAX;
}
/** Replaces one parent with eight children and updates the hierarchy address index.
 * @param undo Receives each overwritten slot for rollback; appended children are undone by restoring the count.
 */
static bool split_leaf(Leaf *leaves,size_t *count,size_t index,const AxScene *scene,uint64_t *refined,LeafEntry *table,size_t *pending,size_t *pending_count,Undo *undo,size_t *undo_count)
{
    if (*count+7>AX_WORLD_MAX_REGIONS || *pending_count+8>LOOKUP_SIZE || *undo_count>=AX_WORLD_MAX_REGIONS) return false;
    Leaf parent=leaves[index]; LeafEntry *entry=lookup(table,&parent.spec);
    if (!entry) return false;
    undo[(*undo_count)++]=(Undo){index,parent};
    entry->index=SIZE_MAX;
    for (size_t c=0;c<8;++c) {
        Leaf child=parent; child.spec.extent*=0.5f;
        double move[3]; for (size_t k=0;k<3;++k) move[k]=(c&(1u<<k)) ? child.spec.extent : 0;
        if (!ax_position_move(&child.spec.origin,move)) return false;
        score_leaf(&child,scene,refined);
        size_t slot=c ? (*count)++ : index;
        leaves[slot]=child; entry=lookup(table,&child.spec);
        if (!entry) return false;
        *entry=(LeafEntry){.origin=child.spec.origin,.planet=child.spec.planet,.extent=child.spec.extent,.index=slot};
        pending[(*pending_count)++]=slot;
    }
    return true;
}
/** Rebuilds the address index from the current leaves, discarding interior and rolled-back entries. */
static bool index_leaves(LeafEntry *table,const Leaf *leaves,size_t count)
{
    memset(table,0,LOOKUP_SIZE*sizeof(*table));
    for (size_t i=0;i<count;++i) {
        LeafEntry *entry=lookup(table,&leaves[i].spec);
        if (!entry) return false;
        *entry=(LeafEntry){.origin=leaves[i].spec.origin,.planet=leaves[i].spec.planet,.extent=leaves[i].spec.extent,.index=i};
    }
    return true;
}
/** Plans a bounded distance-prioritized cut, balances neighboring cells, and reuses matching meshes. */
static bool plan(AxStreamTask *task,const AxWorldStream *stream,const AxScene *scene,AxPlanetId frame,const AxPosition *eye)
{
    const AxPlanet *anchor=ax_scene_planet(scene,frame);
    if (!anchor || scene->count>AX_WORLD_MAX_REGIONS) return false;
    Leaf *leaves=calloc(AX_WORLD_MAX_REGIONS,sizeof(*leaves));
    if (!leaves) return false;
    LeafEntry *table=calloc(LOOKUP_SIZE,sizeof(*table));
    size_t *pending=malloc(LOOKUP_SIZE*sizeof(*pending));
    Undo *undo=malloc(AX_WORLD_MAX_REGIONS*sizeof(*undo));
    uint64_t *refined=refined_cells(stream,scene);
    if (!table || !pending || !undo || !refined) { free(table); free(pending); free(undo); free(refined); free(leaves); return false; }
    size_t count=scene->count;
    bool success=false;
    for (size_t i=0;i<count;++i) {
        const AxPlanet *p=scene->planets[i];
        const AxPlanetDesc *desc=ax_planet_description(p),*a=ax_planet_description(anchor);
        double extent=fmax(ax_planet_extent(p),AX_STREAM_MIN_EXTENT),shift[3]; AxPosition local=*eye;
        for (size_t k=0;k<3;++k) shift[k]=a->center[k]-desc->center[k];
        if (!isfinite(extent) || extent<AX_STREAM_MIN_EXTENT || !ax_position_move(&local,shift)) goto done;
        const AxPosition zero={0}; ax_position_difference(&local,&zero,leaves[i].eye);
        leaves[i].spec=(AxSurfaceRegion){.planet=desc->id,.revision=ax_planet_revision(p),.extent=(float)extent,.cells=AX_SURFACE_CELLS};
        if (!ax_position_from_meters(&leaves[i].spec.origin,(double[3]){-extent/2,-extent/2,-extent/2})) goto done;
        leaves[i].root_extent=extent;
        score_leaf(&leaves[i],scene,refined);
        LeafEntry *entry=lookup(table,&leaves[i].spec);
        if (!entry) goto done;
        *entry=(LeafEntry){.origin=leaves[i].spec.origin,.planet=desc->id,.extent=(float)extent,.index=i};
    }
    while (count+7<=AX_WORLD_MAX_REGIONS) {
        size_t best=SIZE_MAX; double score=1;
        for (size_t i=0;i<count;++i) if (leaves[i].score>score) { best=i; score=leaves[i].score; }
        if (best==SIZE_MAX) break;
        size_t previous_count=count;
        size_t pending_count=0,undo_count=0;
        bool balanced=split_leaf(leaves,&count,best,scene,refined,table,pending,&pending_count,undo,&undo_count);
        for (size_t q=0;balanced && q<pending_count;++q) {
            Leaf fine=leaves[pending[q]];
            if (fine.settled) continue;
            for (int z=-1;balanced && z<=1;++z) for (int y=-1;balanced && y<=1;++y) for (int x=-1;balanced && x<=1;++x) {
                if (!x && !y && !z) continue;
                size_t coarse=coarser_neighbor(table,&fine,x,y,z);
                while (coarse!=SIZE_MAX && !leaves[coarse].uniform && leaves[coarse].spec.extent>fine.spec.extent*2) {
                    balanced=split_leaf(leaves,&count,coarse,scene,refined,table,pending,&pending_count,undo,&undo_count);
                    if (!balanced) break;
                    coarse=coarser_neighbor(table,&fine,x,y,z);
                }
            }
        }
        if (!balanced) {
            while (undo_count) { --undo_count; leaves[undo[undo_count].slot]=undo[undo_count].leaf; }
            count=previous_count;
            leaves[best].score=0;
            if (!index_leaves(table,leaves,count)) goto done;
        }
    }
    if (!index_leaves(table,leaves,count)) goto done;
    task->next=calloc(count,sizeof(*task->next)); task->borrowed=malloc(count*sizeof(*task->borrowed)); task->work=malloc(count*sizeof(*task->work));
    if (!task->next || !task->borrowed || !task->work) goto done;
    task->count=count; task->eye=*eye; task->frame=frame;
    for (size_t i=0;i<count;++i) {
        AxSurfaceRegion spec=leaves[i].spec;
        for (unsigned face=0;face<6;++face) {
            int direction[3]={0}; direction[face/2]=(face&1) ? 1 : -1;
            size_t neighbor=coarser_neighbor(table,&leaves[i],direction[0],direction[1],direction[2]);
            if (neighbor!=SIZE_MAX && !leaves[neighbor].uniform && leaves[neighbor].spec.extent==2*spec.extent) spec.transition_faces|=(uint8_t)(1u<<face);
        }
        if (resolves_caves(spec.extent)) for (int z=-1;z<=1;++z) for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x) {
            if (!x && !y && !z) continue;
            size_t neighbor=coarser_neighbor(table,&leaves[i],x,y,z);
            if (neighbor!=SIZE_MAX && !resolves_caves(leaves[neighbor].spec.extent)) spec.sealed_neighbors|=AX_SURFACE_NEIGHBOR(x,y,z);
        }
        task->next[i]=spec; task->borrowed[i]=SIZE_MAX;
        if (scene->epoch==stream->epoch) for (size_t j=0;j<stream->count;++j) {
            const AxSurfaceRegion *old=&stream->regions[j];
            if (old->planet==spec.planet && old->revision==spec.revision && old->extent==spec.extent && old->transition_faces==spec.transition_faces && old->sealed_neighbors==spec.sealed_neighbors && same_origin(&old->origin,&spec.origin)) {
                task->borrowed[i]=j; task->next[i]=*old; break;
            }
        }
        if (task->borrowed[i]==SIZE_MAX) task->work[task->work_count++]=(MeshWork){task,i};
    }
    success=true;
done:
    free(table); free(pending); free(undo); free(refined); free(leaves); return success;
}
/** Extracts and optimizes one privately owned leaf before publishing completion. */
static void build_region(void *user)
{
    MeshWork *work=user; AxStreamTask *task=work->task;
    AxSurfaceRegion *out=&task->next[work->slot],spec=*out;
    bool ok=ax_surface_build(ax_scene_planet(&task->snapshot,spec.planet),&spec.origin,spec.extent,spec.cells,spec.transition_faces,spec.sealed_neighbors,out);
    if (ok) {
        AxMeshStats stats;
        ok=ax_mesh_optimize(&out->mesh,spec.extent,spec.extent/spec.cells,NULL,&stats);
        if (ok) { out->source_triangles=stats.source_triangles; out->visible_triangles=stats.visible_triangles; out->simplification_error=stats.error; }
    }
    if (!ok) atomic_store_explicit(&task->success,false,memory_order_relaxed);
    atomic_fetch_sub_explicit(&task->remaining,1,memory_order_release);
}
/** Pumps bounded submissions without running terrain work on the render thread. NULL pools run synchronously. */
static void pump(AxStreamTask *task,AxJobs *jobs)
{
    while (task->submitted<task->work_count) {
        MeshWork *work=&task->work[task->submitted];
        if (jobs) { if (!ax_jobs_submit(jobs,build_region,work)) break; }
        else build_region(work);
        ++task->submitted;
    }
}
/** Releases only task-owned meshes; borrowed live meshes remain untouched. */
static void destroy_task(AxStreamTask *task)
{
    if (!task) return;
    if (task->next && task->borrowed) for (size_t i=0;i<task->count;++i) if (task->borrowed[i]==SIZE_MAX) ax_surface_destroy(&task->next[i]);
    free(task->next); free(task->borrowed); free(task->work); ax_scene_destroy(&task->snapshot); free(task);
}
/** Transfers a coherent cut and relinquishes each borrowed mesh from the old cut. */
static void commit(AxWorldStream *stream,AxStreamTask *task)
{
    for (size_t i=0;i<task->count;++i) if (task->borrowed[i]!=SIZE_MAX) memset(&stream->regions[task->borrowed[i]].mesh,0,sizeof(AxSurfaceMesh));
    for (size_t i=0;i<stream->count;++i) ax_surface_destroy(&stream->regions[i]);
    free(stream->regions); stream->regions=task->next; task->next=NULL;
    stream->count=task->count; stream->source_count=task->snapshot.count; stream->epoch=task->snapshot.epoch; stream->planned_eye=task->eye; stream->planned_frame=task->frame; stream->dirty=true;
}
/** Checks all source revisions before publishing asynchronous geometry. */
static bool current_sources(const AxScene *scene,const AxStreamTask *task)
{
    if (scene->count!=task->snapshot.count || scene->epoch!=task->snapshot.epoch) return false;
    for (size_t i=0;i<scene->count;++i) {
        const AxPlanet *p=task->snapshot.planets[i],*live=ax_scene_planet(scene,ax_planet_description(p)->id);
        if (!live || ax_planet_revision(p)!=ax_planet_revision(live)) return false;
    }
    return true;
}
/** Plans against borrowed immutable live meshes and an owned source snapshot on one worker. */
static void build_plan(void *user)
{
    AxStreamTask *task=user;
    if (!plan(task,&task->previous,&task->snapshot,task->frame,&task->eye)) {
        atomic_store_explicit(&task->success,false,memory_order_relaxed); task->work_count=0;
    }
    atomic_store_explicit(&task->remaining,task->work_count,memory_order_relaxed);
    atomic_store_explicit(&task->planned,true,memory_order_release);
}
/** Submits hierarchy planning once without blocking or doing it inline under queue pressure. */
static void submit_plan(AxStreamTask *task,AxJobs *jobs)
{
    if (task->planning_submitted) return;
    if (jobs) { if (!ax_jobs_submit(jobs,build_plan,task)) return; }
    else build_plan(task);
    task->planning_submitted=true;
}
/** Captures stable metadata and immutable source data before asynchronous planning starts. */
static AxStreamTask *new_task(const AxWorldStream *stream,const AxScene *scene,AxPlanetId frame,const AxPosition *eye)
{
    AxStreamTask *task=calloc(1,sizeof(*task));
    if (!task) return NULL;
    task->previous=*stream; task->frame=frame; task->eye=*eye;
    atomic_init(&task->success,true); atomic_init(&task->remaining,0); atomic_init(&task->planned,false);
    if (!ax_scene_clone(scene,&task->snapshot)) { destroy_task(task); return NULL; }
    return task;
}
bool ax_world_stream_create(AxWorldStream *stream,const AxScene *scene,AxPlanetId planet,const AxPosition *eye,AxJobs *jobs)
{
    if (!stream || stream->count || stream->task || !scene || !scene->count || !eye) return false;
    AxStreamTask *task=new_task(stream,scene,planet,eye);
    if (!task) return false;
    build_plan(task);
    do { pump(task,jobs); if (jobs) ax_jobs_wait(jobs); } while (atomic_load_explicit(&task->remaining,memory_order_acquire));
    bool ok=atomic_load_explicit(&task->success,memory_order_relaxed);
    if (ok) commit(stream,task);
    destroy_task(task); return ok;
}
bool ax_world_stream_update(AxWorldStream *stream,const AxScene *scene,AxPlanetId frame,const AxPosition *eye,AxJobs *jobs)
{
    if (!stream || !scene || !eye || !stream->count) return false;
    if (stream->task) {
        AxStreamTask *task=stream->task;
        submit_plan(task,jobs);
        if (!atomic_load_explicit(&task->planned,memory_order_acquire)) return true;
        if (!current_sources(scene,task)) {
            size_t unsent=task->work_count-task->submitted;
            task->work_count=task->submitted;
            atomic_fetch_sub_explicit(&task->remaining,unsent,memory_order_release);
        }
        pump(task,jobs);
        if (atomic_load_explicit(&task->remaining,memory_order_acquire)) return true;
        bool ok=atomic_load_explicit(&task->success,memory_order_relaxed);
        if (ok && current_sources(scene,task)) {
            if (!task->work_count && task->count==stream->count) { stream->planned_eye=task->eye; stream->planned_frame=task->frame; }
            else commit(stream,task);
        }
        destroy_task(task); stream->task=NULL;
        if (!ok) return false;
    }
    double delta[3]; ax_position_difference(eye,&stream->planned_eye,delta);
    bool changed=scene->count!=stream->source_count || scene->epoch!=stream->epoch || frame!=stream->planned_frame || hypot(hypot(delta[0],delta[1]),delta[2])>=AX_STREAM_REPLAN_DISTANCE;
    for (size_t i=0;i<stream->count && !changed;++i) {
        const AxPlanet *p=ax_scene_planet(scene,stream->regions[i].planet);
        if (!p || ax_planet_revision(p)!=stream->regions[i].revision) changed=true;
    }
    if (!changed) return true;
    AxStreamTask *task=new_task(stream,scene,frame,eye);
    if (!task) return false;
    stream->task=task; submit_plan(task,jobs);
    return true;
}
void ax_world_stream_destroy(AxWorldStream *stream,AxJobs *jobs)
{
    if (!stream) return;
    if (stream->task && jobs) ax_jobs_wait(jobs);
    destroy_task(stream->task);
    for (size_t i=0;i<stream->count;++i) ax_surface_destroy(&stream->regions[i]);
    free(stream->regions); *stream=(AxWorldStream){0};
}
