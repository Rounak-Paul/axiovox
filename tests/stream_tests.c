#include "world/stream.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%d: %s\n",__LINE__,#c); goto cleanup; } } while (0)
/** Completes bounded submissions and coherent publication, including a replacement after stale work. */
static bool settle(AxWorldStream *stream,const AxScene *scene,const AxPosition *eye,AxJobs *jobs)
{
    for (size_t i=0;i<64;++i) {
        if (!ax_world_stream_update(stream,scene,1,eye,jobs)) return false;
        if (!stream->task) return true;
        ax_jobs_wait(jobs);
    }
    return false;
}
/** Returns the smallest leaf covering a fixed terrain point, independent of array ordering. */
static float extent_at(const AxWorldStream *stream,AxPlanetId planet,const double point[3])
{
    float result=INFINITY; const AxPosition zero={0};
    for (size_t i=0;i<stream->count;++i) {
        const AxSurfaceRegion *r=&stream->regions[i]; double origin[3]; ax_position_difference(&r->origin,&zero,origin);
        bool inside=r->planet==planet;
        for (size_t k=0;k<3;++k) if (point[k]<origin[k] || point[k]>=origin[k]+r->extent) inside=false;
        if (inside) result=fminf(result,r->extent);
    }
    return result;
}
/** Checks nonoverlap, all-touch 2:1 balance and face masks across the published cut. */
static bool valid_cut(const AxWorldStream *stream)
{
    const AxPosition zero={0};
    for (size_t i=0;i<stream->count;++i) for (size_t j=i+1;j<stream->count;++j) {
        const AxSurfaceRegion *a=&stream->regions[i],*b=&stream->regions[j];
        if (a->planet!=b->planet) continue;
        double p[3],q[3]; ax_position_difference(&a->origin,&zero,p); ax_position_difference(&b->origin,&zero,q);
        bool apart=false; int touching=0,face=-1;
        for (size_t k=0;k<3;++k) {
            double low=fmax(p[k],q[k]),high=fmin(p[k]+a->extent,q[k]+b->extent);
            if (low>high) apart=true;
            if (low==high) { ++touching; face=(int)(2*k)+(p[k]<q[k]); }
        }
        if (apart) continue;
        if (!touching) return false;
        if (!a->mesh.index_count || !b->mesh.index_count) continue;
        if (a->extent>2*b->extent || b->extent>2*a->extent) return false;
        if (touching==1 && a->extent*2==b->extent && !(a->transition_faces&(1u<<face))) return false;
        if (touching==1 && b->extent*2==a->extent && !(b->transition_faces&(1u<<(face^1)))) return false;
    }
    return true;
}
/** Verifies deterministic parallel meshing, lateral distance LOD, both bodies, edits and stale snapshots. */
int main(void)
{
    int result=EXIT_FAILURE; AxScene scene={0}; AxWorldStream stream={0},serial={0}; AxWorldGeometry geometry={0};
    AxJobs *jobs=ax_jobs_create(2); CHECK(jobs);
    AxPlanetDesc terra={.id=1,.seed=42,.radius=128,.terrain_height=4,.terrain_frequency=0.03,.terrain_kind=AX_TERRAIN_CONTINENTAL,.cave_depth=8};
    AxPlanetDesc luna={.id=2,.seed=917,.center={512,256,-512},.radius=32,.surface_material=AX_MATERIAL_ROCK};
    CHECK(ax_scene_add_planet(&scene,&terra) && ax_scene_add_planet(&scene,&luna));
    AxPosition eye; CHECK(ax_position_from_meters(&eye,(double[3]){1,132,1}));
    CHECK(ax_world_stream_create(&stream,&scene,1,&eye,jobs));
    CHECK(ax_world_stream_create(&serial,&scene,1,&eye,NULL));
    CHECK(stream.count==serial.count && stream.count<=AX_WORLD_MAX_REGIONS);
    for (size_t i=0;i<stream.count;++i) {
        const AxSurfaceMesh *a=&stream.regions[i].mesh,*b=&serial.regions[i].mesh;
        CHECK(a->vertex_count==b->vertex_count && a->index_count==b->index_count);
        if (a->vertex_count) CHECK(!memcmp(a->vertices,b->vertices,a->vertex_count*sizeof(*a->vertices)));
        if (a->index_count) CHECK(!memcmp(a->indices,b->indices,a->index_count*sizeof(*a->indices)));
    }
    ax_world_stream_destroy(&serial,NULL);
    CHECK(valid_cut(&stream));
    float near=extent_at(&stream,1,(double[3]){1,128,1}); CHECK(near<=16);
    CHECK(extent_at(&stream,2,(double[3]){1,31,1})<=luna.radius);
    CHECK(ax_world_geometry_build(&scene,stream.regions,stream.count,&geometry));
    CHECK(geometry.count==stream.count);
    for (size_t i=0;i<geometry.count;++i) CHECK((size_t)geometry.regions[i].first_index+geometry.regions[i].index_count<=geometry.index_count);
    CHECK(ax_position_move(&eye,(double[3]){160,0,0}));
    CHECK(settle(&stream,&scene,&eye,jobs));
    CHECK(valid_cut(&stream));
    float far=extent_at(&stream,1,(double[3]){1,128,1}); CHECK(far>near);
    CHECK(ax_position_move(&eye,(double[3]){-160,0,0}));
    CHECK(settle(&stream,&scene,&eye,jobs)); CHECK(extent_at(&stream,1,(double[3]){1,128,1})<far);
    CHECK(ax_position_move(&eye,(double[3]){48,0,0}));
    CHECK(ax_world_stream_update(&stream,&scene,1,&eye,jobs) && stream.task);
    AxVolumeEdit edit={.position=eye,.half_extent={2,2,2},.material=AX_MATERIAL_BUILDING}; CHECK(ax_scene_edit(&scene,1,&edit));
    CHECK(settle(&stream,&scene,&eye,jobs));
    for (size_t i=0;i<stream.count;++i) if (stream.regions[i].planet==1) CHECK(stream.regions[i].revision==2);
    CHECK(ax_position_from_meters(&eye,(double[3]){513,289,-511})); CHECK(settle(&stream,&scene,&eye,jobs));
    CHECK(extent_at(&stream,2,(double[3]){1,31,1})<=16);
    CHECK(isfinite(extent_at(&stream,1,(double[3]){1,128,1})));
    CHECK(ax_scene_save(&scene,"stream-test-save.tmp"));
    CHECK(ax_position_move(&eye,(double[3]){48,0,0})); CHECK(ax_world_stream_update(&stream,&scene,1,&eye,jobs));
    uint64_t epoch=scene.epoch; CHECK(ax_scene_load(&scene,"stream-test-save.tmp") && scene.epoch==epoch+1);
    CHECK(settle(&stream,&scene,&eye,jobs) && stream.epoch==scene.epoch);
    CHECK(ax_world_geometry_build(&scene,stream.regions,stream.count,&geometry));
    AxPlanetDesc added={.id=3,.center={10000,0,0},.radius=1};
    CHECK(ax_scene_add_planet(&scene,&added)); CHECK(settle(&stream,&scene,&eye,jobs));
    CHECK(isfinite(extent_at(&stream,3,(double[3]){0,0,0})) && valid_cut(&stream));
    CHECK(ax_position_move(&eye,(double[3]){48,0,0}));
    CHECK(ax_world_stream_update(&stream,&scene,1,&eye,jobs));
    result=EXIT_SUCCESS;
cleanup:
    remove("stream-test-save.tmp"); ax_world_stream_destroy(&serial,NULL); ax_world_stream_destroy(&stream,jobs); ax_jobs_destroy(jobs);
    ax_world_geometry_destroy(&geometry); ax_scene_destroy(&scene); return result;
}
