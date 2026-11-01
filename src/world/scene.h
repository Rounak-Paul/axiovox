#pragma once
#include "coordinates.h"
#include "terrain.h"
#include <stddef.h>
#include "core/event.h"
#define AX_SCENE_MAX_PLANETS 4096u
#define AX_PLANET_MAX_EDITS 1000000u
/** Stable identity; zero is invalid. Planet positions are relative to their owning star system. */
typedef uint64_t AxPlanetId;
typedef enum { AX_MATERIAL_AIR, AX_MATERIAL_ROCK, AX_MATERIAL_SOIL, AX_MATERIAL_GRASS, AX_MATERIAL_BUILDING, AX_MATERIAL_SAND, AX_MATERIAL_SNOW, AX_MATERIAL_COUNT } AxMaterial;
typedef struct {
    AxPlanetId id;
    uint64_t seed;
    double center[3], radius, terrain_height, terrain_frequency;
    AxMaterial surface_material;
    AxTerrainKind terrain_kind;
    double surface_gravity, cave_depth;
} AxPlanetDesc;
typedef enum { AX_BRUSH_SPHERE, AX_BRUSH_BOX } AxBrushShape;
/** Ordered solid CSG operation. Half extents are meters; sphere uses half_extent[0]. */
typedef struct {
    AxPosition position;
    double half_extent[3];
    AxBrushShape shape;
    AxMaterial material;
    bool remove;
} AxVolumeEdit;
typedef struct AxPlanet AxPlanet;
typedef struct {
    AxPlanet **planets;
    size_t count, capacity;
    uint64_t epoch;
    AxEvents *events;
    bool notifying;
} AxScene;
/** Initializes an empty scene; events is borrowed and may be NULL. Owner-thread mutations only. */
void ax_scene_init(AxScene *scene, AxEvents *events);
/** Releases planets and edits; region assets are owned separately. Accepts NULL/zero state. */
void ax_scene_destroy(AxScene *scene);
/** Adds a validated planet with unique nonzero identity; returns NULL without changes on failure. */
AxPlanet *ax_scene_add_planet(AxScene *scene, const AxPlanetDesc *desc);
/** Finds a planet by identity; returned pointers survive adding other planets. */
const AxPlanet *ax_scene_planet(const AxScene *scene, AxPlanetId id);
/** Returns immutable generation metadata; planet must be valid. */
const AxPlanetDesc *ax_planet_description(const AxPlanet *planet);
/** Returns the current content revision; planet must be valid. */
uint64_t ax_planet_revision(const AxPlanet *planet);
/** Samples signed solid field and material, including edits; negative means solid. Safe for concurrent reads only. */
double ax_planet_sample(const AxPlanet *planet, const AxPosition *position, AxMaterial *material);
/** Samples the field for meshing; caves=false omits procedural channels on lattices too coarse to resolve them. Edits always apply. */
double ax_planet_sample_lod(const AxPlanet *planet, const AxPosition *position, bool caves);
/** Appends a validated edit and emits AX_EVENT_WORLD_CHANGED after commit; rejects reentrant mutation. */
bool ax_scene_edit(AxScene *scene, AxPlanetId id, const AxVolumeEdit *edit);
/** Returns radial local-up; returns false at planet center or on invalid coordinates. */
bool ax_planet_up(const AxPosition *position, double up[3]);
/** Atomically saves planet metadata and edits in a versioned portable text format. */
bool ax_scene_save(const AxScene *scene, const char *path);
/** Transactionally replaces scene from a validated save; preserves event binding and emits a reset event (planet=0). */
bool ax_scene_load(AxScene *scene, const char *path);
/** Copies one planet and its edits into an empty independent scene for immutable worker sampling. */
bool ax_scene_copy_planet(const AxScene *scene, AxPlanetId id, AxScene *copy);
/** Returns a power-of-two cube extent enclosing the planet and all additive edits, centered at its origin. */
double ax_planet_extent(const AxPlanet *planet);
/** Returns a stable human-readable name for the built-in Terra/Luna identities. */
const char *ax_planet_name(AxPlanetId id);

/** Copies all planets/edits transactionally into an independent snapshot, without event subscriptions. */
bool ax_scene_clone(const AxScene *scene, AxScene *copy);

/** Bounds density throughout a cube conservatively, including saved edits and, when caves is set, procedural channels. */
bool ax_planet_density_bounds(const AxPlanet *planet,const AxPosition *origin,double extent,bool caves,double *minimum,double *maximum);

/** Estimates whether a cube is worth refining, by sampling terrain height across it and testing edit brushes.
 * Not conservative: a miss only leaves that cube at its current resolution, so never use this to omit geometry.
 * @param caves Also counts the procedural channel band beneath the surface.
 */
bool ax_planet_surface_likely(const AxPlanet *planet,const AxPosition *origin,double extent,bool caves);

/** Reports whether terrain inside a cube is a radial surface, which must cross the cube boundary to enter it.
 * True only when the cube excludes the planet center and no edit can form a closed pocket inside it. Procedural
 * caves are not considered; they form pockets only inside rock, so a caller must treat a solid boundary accordingly.
 */
bool ax_planet_surface_crosses_boundary(const AxPlanet *planet,const AxPosition *origin,double extent);

/** Returns unedited radial terrain displacement at a planet-local position. */
double ax_planet_terrain_height(const AxPlanet *planet,const double position[3]);

/** Returns the packed ABGR color of an extracted surface point, blending climate and slope layers continuously
 * so material borders stay smooth at every mesh resolution.
 * @param caves Must match the field that produced the surface, so channel walls color as rock only where they exist.
 * @param normal Unit outward surface normal.
 */
uint32_t ax_planet_surface_color(const AxPlanet *planet,const AxPosition *position,bool caves,const double normal[3]);
