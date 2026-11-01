#include "world/stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d: %s\n", __LINE__, #c); goto cleanup; } } while (0)
/** Verifies immutable worker snapshots, continuous coordinate movement, stale-job rejection and packed forest addresses. */
int main(void)
{
    int result = EXIT_FAILURE;
    AxScene scene = {0}, snapshot = {0};
    AxWorldStream stream = {0},serial={0};
    AxWorldGeometry geometry = {0};
    AxJobs *jobs = ax_jobs_create(2);
    CHECK(jobs);
    AxPlanetDesc terra = {.id = 1, .seed = 42, .radius = 128, .terrain_height=4, .terrain_frequency=0.03,
        .terrain_kind=AX_TERRAIN_CONTINENTAL, .cave_depth=8};
    AxPlanetDesc luna = {.id = 2, .seed = 917, .center = {512,256,-512}, .radius = 32, .surface_material = AX_MATERIAL_ROCK};
    CHECK(ax_scene_add_planet(&scene, &terra) && ax_scene_add_planet(&scene, &luna));
    AxPosition eye;
    CHECK(ax_position_from_meters(&eye, (double[3]){0,140,0}));
    CHECK(ax_world_stream_create(&stream, &scene, 1, &eye, jobs));
    CHECK(ax_world_stream_create(&serial,&scene,1,&eye,NULL));
    for (size_t i=0;i<stream.count;++i) {
        const AxSurfaceMesh *a=&stream.regions[i].mesh,*b=&serial.regions[i].mesh;
        CHECK(a->vertex_count==b->vertex_count && a->index_count==b->index_count);
        if (a->vertex_count) CHECK(!memcmp(a->vertices,b->vertices,a->vertex_count*sizeof(*a->vertices)));
        if (a->index_count) CHECK(!memcmp(a->indices,b->indices,a->index_count*sizeof(*a->indices)));
    }
    ax_world_stream_destroy(&serial,NULL);
    CHECK(stream.count == AX_STREAM_DETAIL_LEVELS + 2 && stream.regions[AX_STREAM_DETAIL_LEVELS].planet == 1 && stream.regions[AX_STREAM_DETAIL_LEVELS + 1].planet == 2);
    CHECK(ax_world_geometry_build(&scene, stream.regions, stream.count, &geometry));
    CHECK(geometry.count == 6 && geometry.regions[0].extent == 32 && geometry.regions[4].extent == 512);
    for (size_t i = 0; i < geometry.count; ++i) {
        const AxWorldRegion *r = &geometry.regions[i];
        CHECK(r->first_index + r->index_count <= geometry.index_count && (size_t)r->vertex_offset < geometry.vertex_count);
    }
    AxPosition old = stream.regions[0].origin;
    CHECK(ax_position_move(&eye, (double[3]){48,0,0}));
    CHECK(ax_world_stream_update(&stream, &scene, 1, &eye, jobs) && stream.task);
    CHECK(ax_scene_copy_planet(&scene, 1, &snapshot));
    AxVolumeEdit edit = {.position = eye, .half_extent = {2,2,2}, .material = AX_MATERIAL_BUILDING};
    CHECK(ax_scene_edit(&scene, 1, &edit));
    CHECK(ax_planet_revision(ax_scene_planet(&snapshot, 1)) == 1);
    CHECK(ax_planet_revision(ax_scene_planet(&scene, 1)) == 2);
    ax_jobs_wait(jobs);
    CHECK(ax_world_stream_update(&stream, &scene, 1, &eye, jobs));
    CHECK(stream.regions[0].revision == 1);
    for (size_t i = 0; i < AX_WORLD_MAX_REGIONS; ++i) {
        ax_jobs_wait(jobs);
        CHECK(ax_world_stream_update(&stream, &scene, 1, &eye, jobs));
        if (!stream.task) break;
    }
    CHECK(!stream.task && stream.regions[0].revision == 2 && stream.regions[AX_STREAM_DETAIL_LEVELS].revision == 2);
    CHECK(memcmp(&old, &stream.regions[0].origin, sizeof(old)));
    AxPosition luna_eye;
    CHECK(ax_position_from_meters(&luna_eye, (double[3]){512,300,-512}));
    for (size_t i = 0; i < AX_WORLD_MAX_REGIONS; ++i) {
        CHECK(ax_world_stream_update(&stream, &scene, 1, &luna_eye, jobs));
        ax_jobs_wait(jobs);
        if (stream.regions[0].planet == 2 && !stream.task) break;
    }
    CHECK(stream.regions[0].planet == 2 && stream.regions[AX_STREAM_DETAIL_LEVELS].planet == 1 && stream.regions[AX_STREAM_DETAIL_LEVELS + 1].planet == 2);
    CHECK(ax_world_geometry_build(&scene, stream.regions, stream.count, &geometry));
    CHECK(geometry.count >= 3);
    AxVolumeEdit distant = {.half_extent = {10,1,1}, .material = AX_MATERIAL_BUILDING};
    CHECK(ax_position_from_meters(&distant.position,(double[3]){0,1020,0}));
    CHECK(ax_scene_edit(&scene,2,&distant));
    CHECK(ax_planet_extent(ax_scene_planet(&scene,2)) == 4096);
    CHECK(ax_scene_save(&scene,"stream-test-save.tmp"));
    CHECK(ax_world_stream_update(&stream,&scene,1,&luna_eye,jobs) && stream.task);
    uint64_t old_epoch=scene.epoch;
    CHECK(ax_scene_load(&scene,"stream-test-save.tmp") && scene.epoch==old_epoch+1);
    ax_jobs_wait(jobs);
    CHECK(ax_world_stream_update(&stream,&scene,1,&luna_eye,jobs) && stream.epoch==old_epoch);
    ax_jobs_wait(jobs);
    CHECK(ax_world_stream_update(&stream,&scene,1,&luna_eye,jobs) && stream.epoch==scene.epoch);
    CHECK(remove("stream-test-save.tmp")==0);
    result = EXIT_SUCCESS;
cleanup:
    remove("stream-test-save.tmp");
    ax_world_stream_destroy(&serial,NULL);
    ax_world_stream_destroy(&stream, jobs);
    ax_jobs_destroy(jobs);
    ax_world_geometry_destroy(&geometry);
    ax_scene_destroy(&snapshot);
    ax_scene_destroy(&scene);
    return result;
}
