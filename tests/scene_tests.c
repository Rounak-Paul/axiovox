#include "world/scene.h"
#include "core/files.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d: %s\n", __LINE__, #c); return false; } } while (0)
/** Exercises negative boundaries, exact neighboring addresses far beyond double integer precision, and overflow rollback. */
static bool coordinates(void)
{
    AxPosition p;
    CHECK(ax_position_from_meters(&p, (double[3]){-0.25, -2, 2.25}));
    CHECK(p.chunk[0] == -1 && p.offset[0] == 1.75 && p.chunk[1] == -1 && p.offset[1] == 0);
    AxPosition a = {.chunk = {INT64_MAX - 1, INT64_MIN, 0}, .offset = {0.125, 0, 0}};
    AxPosition b = a;
    ++b.chunk[0]; b.offset[0] = 0.25;
    double delta[3];
    ax_position_difference(&b, &a, delta);
    CHECK(delta[0] == 2.125 && delta[1] == 0);
    CHECK(ax_position_move(&a, (double[3]){0.0625, 0, 0}));
    CHECK(a.offset[0] == 0.1875);
    a.chunk[0] = INT64_MAX;
    AxPosition before = a;
    CHECK(!ax_position_move(&a, (double[3]){2, 0, 0}) && !memcmp(&a, &before, sizeof(a)));
    CHECK(!ax_position_move(&a, (double[3]){NAN, 0, 0}));
    CHECK(!ax_position_from_meters(&a, (double[3]){INFINITY, 0, 0}));
    return true;
}
typedef struct { AxScene *scene; unsigned changes; bool blocked; } Observer;
/** Checks committed reads and blocked reentrant writes during change notification. */
static void observe(const AxEvent *event, void *user)
{
    Observer *o = user;
    ++o->changes;
    if (event->data.world.planet) {
        const AxPlanet *planet = ax_scene_planet(o->scene, event->data.world.planet);
        o->blocked = planet && ax_planet_revision(planet) == event->data.world.revision &&
                     !ax_scene_add_planet(o->scene, &(AxPlanetDesc){.id = 99, .radius = 10});
    }
}
/** Composes excavation and construction, verifies seeds, stable ownership, revisions, and solid interior queries. */
static bool volume(void)
{
    AxEvents events = {0};
    AxScene scene;
    ax_scene_init(&scene, &events);
    Observer observer = {.scene = &scene};
    CHECK(ax_event_subscribe(&events, AX_EVENT_WORLD_CHANGED, observe, &observer));
    AxPlanetDesc desc = {.id = 1, .seed = 42, .radius = 1000000};
    const AxPlanet *planet = ax_scene_add_planet(&scene, &desc);
    CHECK(planet && observer.blocked && observer.changes == 1);
    CHECK(!ax_scene_add_planet(&scene, &desc));
    desc.id = 2; desc.terrain_height = 8; desc.terrain_frequency = 0.04;
    const AxPlanet *other = ax_scene_add_planet(&scene, &desc);
    CHECK(other && planet == ax_scene_planet(&scene, 1));
    AxPosition center = {0}, surface;
    CHECK(ax_position_from_meters(&surface, (double[3]){0, 1000000, 0}));
    AxMaterial material;
    CHECK(ax_planet_sample(planet, &center, &material) == -1000000 && material == AX_MATERIAL_ROCK);
    CHECK(ax_planet_sample(planet, &surface, NULL) == 0);
    desc.id = 3;
    const AxPlanet *same = ax_scene_add_planet(&scene, &desc);
    CHECK(same && ax_planet_sample(other, &surface, NULL) == ax_planet_sample(same, &surface, NULL));
    desc.id = 4; ++desc.seed;
    const AxPlanet *different = ax_scene_add_planet(&scene, &desc);
    CHECK(different && ax_planet_sample(other, &surface, NULL) != ax_planet_sample(different, &surface, NULL));
    double up[3];
    CHECK(!ax_planet_up(&center, up));
    CHECK(ax_planet_up(&surface, up) && up[1] == 1 && up[0] == 0);
    AxVolumeEdit carve = {.position = surface, .half_extent = {4,4,4}, .shape = AX_BRUSH_SPHERE,
                          .material = AX_MATERIAL_ROCK, .remove = true};
    CHECK(ax_scene_edit(&scene, 1, &carve));
    CHECK(ax_planet_sample(planet, &surface, &material) == 4 && material == AX_MATERIAL_AIR);
    AxPosition deep = surface;
    CHECK(ax_position_move(&deep, (double[3]){0, -6, 0}));
    CHECK(ax_planet_sample(planet, &deep, &material) < 0 && material == AX_MATERIAL_ROCK);
    AxVolumeEdit build = carve;
    build.remove = false; build.shape = AX_BRUSH_BOX; build.material = AX_MATERIAL_BUILDING;
    for (size_t i = 0; i < 3; ++i) build.half_extent[i] = 1;
    CHECK(ax_scene_edit(&scene, 1, &build));
    CHECK(ax_planet_sample(planet, &surface, &material) == -1 && material == AX_MATERIAL_BUILDING);
    uint64_t revision = ax_planet_revision(planet);
    build.half_extent[0] = NAN;
    CHECK(!ax_scene_edit(&scene, 1, &build) && ax_planet_revision(planet) == revision);
    CHECK(ax_planet_sample(other, &center, NULL) < 0);
    CHECK(observer.changes == 6 && observer.blocked);
    ax_scene_destroy(&scene);
    return true;
}
/** Confirms exact save/load content and transactional rejection of truncated, overflowing, and trailing records. */
static bool persistence(void)
{
    const char *path = "scene-test-save.tmp";
    AxScene scene = {0}, loaded = {0};
    AxPlanetDesc desc = {.id = UINT64_MAX, .seed = UINT64_MAX, .radius = 400000, .terrain_height = 6, .terrain_frequency = 0.03, .terrain_kind=AX_TERRAIN_CONTINENTAL, .surface_gravity=9.81, .cave_depth=24};
    CHECK(ax_scene_add_planet(&scene, &desc));
    AxVolumeEdit edit = {.position = {.chunk = {INT64_MAX - 1, INT64_MIN + 1, 0}, .offset = {0.0625, 1.5, 0}},
        .half_extent = {1, 2, 3}, .shape = AX_BRUSH_BOX, .material = AX_MATERIAL_BUILDING};
    CHECK(ax_scene_edit(&scene, desc.id, &edit));
    CHECK(ax_scene_save(&scene, path));
    CHECK(ax_scene_load(&loaded, path) && loaded.count == 1);
    const AxPlanet *p = ax_scene_planet(&loaded, desc.id);
    AxMaterial material;
    if (p) {
        AxPosition high,sea,deep_rock;
        CHECK(ax_position_from_meters(&high,(double[3]){-8,400100,-8}) && ax_position_from_meters(&sea,(double[3]){-8,399992,-8}) && ax_position_from_meters(&deep_rock,(double[3]){-8,390000,-8}));
        CHECK(!ax_planet_surface_likely(p,&high,16,true) && ax_planet_surface_likely(p,&sea,16,false) && !ax_planet_surface_likely(p,&deep_rock,16,true));
    }
    CHECK(p && ax_planet_description(p)->terrain_kind==AX_TERRAIN_CONTINENTAL && ax_planet_description(p)->surface_gravity==9.81 && ax_planet_description(p)->cave_depth==24);
    CHECK(p && ax_planet_sample(p, &edit.position, &material) < 0 && material == AX_MATERIAL_BUILDING);
    const char *invalid[] = {"AXSCENE 99 0\n", "AXSCENE 1 1\n", "AXSCENE 1 18446744073709551616\n",
        "AXSCENE 1 0\ngarbage", "AXSCENE 1 1\nP 1 1 0 0 0 nan 0 0 0\n"};
    for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        FILE *f = ax_file_open(path, "wb");
        CHECK(f && fputs(invalid[i], f) >= 0 && fclose(f) == 0);
        CHECK(!ax_scene_load(&loaded, path) && ax_scene_planet(&loaded, desc.id) == p);
    }
    FILE *legacy=ax_file_open(path,"wb");
    CHECK(legacy && fputs("AXSCENE 2 1\nP 9 42 0 0 0 100 6 0.03 0 0\n",legacy)>=0 && fclose(legacy)==0);
    CHECK(ax_scene_load(&loaded,path));
    p=ax_scene_planet(&loaded,9); CHECK(p && ax_planet_description(p)->terrain_kind==AX_TERRAIN_WAVES);
    AxPosition test_point; CHECK(ax_position_from_meters(&test_point,(double[3]){1,100,3}));
    double wave=6;
    const double coordinates[3]={1,100,3};
    for (size_t k=0;k<3;++k) wave*=sin(coordinates[k]*0.03+(double)(ax_noise_hash64(42+k)>>11)*0x1p-53*6.283185307179586);
    CHECK(fabs(ax_planet_sample(p,&test_point,NULL)-(sqrt(10010)-100-wave))<1e-10);
    CHECK(remove(path) == 0);
    ax_scene_destroy(&scene); ax_scene_destroy(&loaded);
    return true;
}
/** Runs CPU-only scene, precision, volume, derived-asset, and persistence regressions. */
int main(void) { return coordinates() && volume() && persistence() ? EXIT_SUCCESS : EXIT_FAILURE; }
