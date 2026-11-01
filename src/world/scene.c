#include "scene.h"
#include "core/files.h"
#include <inttypes.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
struct AxPlanet {
    AxPlanetDesc desc;
    AxVolumeEdit *edits;
    size_t count, capacity;
    uint64_t revision;
    AxTerrain terrain;
};
/** Expands a dynamic array with checked size arithmetic; leaves storage unchanged on failure. */
static bool reserve(void **data, size_t *capacity, size_t count, size_t size)
{
    if (count < *capacity) return true;
    size_t next = *capacity ? *capacity * 2 : 8;
    if (next < *capacity || next > SIZE_MAX / size) return false;
    void *allocation = realloc(*data, next * size);
    if (!allocation) return false;
    *data = allocation;
    *capacity = next;
    return true;
}
/** Validates scene placement, gravity and material metadata; terrain owns recipe validation. */
static bool valid_description(const AxPlanetDesc *d)
{
    if (!d || !d->id || d->surface_material<AX_MATERIAL_AIR || d->surface_material>=AX_MATERIAL_COUNT ||
        !isfinite(d->surface_gravity) || d->surface_gravity<0 || d->surface_gravity>10000) return false;
    for (size_t i=0;i<3;++i) if (!isfinite(d->center[i])) return false;
    return true;
}
/** Dispatches committed scene changes while preventing mutation during borrowed reads. */
static void changed(AxScene *scene, AxPlanetId id, uint64_t revision)
{
    scene->notifying = true;
    AxEvent event = {.type = AX_EVENT_WORLD_CHANGED, .data.world = {id, revision}};
    ax_event_emit(scene->events, &event);
    scene->notifying = false;
}
void ax_scene_init(AxScene *scene, AxEvents *events)
{
    if (scene) *scene = (AxScene){.events = events};
}
void ax_scene_destroy(AxScene *scene)
{
    if (!scene || scene->notifying) return;
    for (size_t i = 0; i < scene->count; ++i) { free(scene->planets[i]->edits); free(scene->planets[i]); }
    free(scene->planets);
    *scene = (AxScene){0};
}
const AxPlanet *ax_scene_planet(const AxScene *scene, AxPlanetId id)
{
    if (scene && id) for (size_t i = 0; i < scene->count; ++i) if (scene->planets[i]->desc.id == id) return scene->planets[i];
    return NULL;
}
AxPlanet *ax_scene_add_planet(AxScene *scene, const AxPlanetDesc *desc)
{
    if (!scene || scene->notifying || scene->count >= AX_SCENE_MAX_PLANETS || !valid_description(desc) || ax_scene_planet(scene, desc->id)) return NULL;
    AxPlanet *planet = calloc(1, sizeof(*planet));
    if (!planet) return NULL;
    if (!ax_terrain_init(&planet->terrain,desc->terrain_kind,desc->seed,desc->radius,desc->terrain_height,desc->terrain_frequency,desc->cave_depth)) {
        free(planet); return NULL;
    }
    if (!reserve((void **)&scene->planets, &scene->capacity, scene->count, sizeof(*scene->planets))) { free(planet); return NULL; }
    planet->desc = *desc;
    planet->revision = 1;
    scene->planets[scene->count++] = planet;
    changed(scene, desc->id, planet->revision);
    return planet;
}
const AxPlanetDesc *ax_planet_description(const AxPlanet *planet) { return &planet->desc; }
uint64_t ax_planet_revision(const AxPlanet *planet) { return planet->revision; }
/** Computes signed distance to a sphere or axis-aligned box in edit-local meters. */
static double brush_distance(const AxVolumeEdit *edit, const AxPosition *position)
{
    double p[3];
    ax_position_difference(position, &edit->position, p);
    if (edit->shape == AX_BRUSH_SPHERE) return hypot(hypot(p[0], p[1]), p[2]) - edit->half_extent[0];
    double q[3];
    for (size_t i = 0; i < 3; ++i) q[i] = fabs(p[i]) - edit->half_extent[i];
    return hypot(hypot(fmax(q[0], 0), fmax(q[1], 0)), fmax(q[2], 0)) + fmin(fmax(q[0], fmax(q[1], q[2])), 0);
}
/** Applies authored brushes over a field in order, reporting the material of the brush that won the final surface. */
static double apply_edits(const AxPlanet *planet,const AxPosition *position,double field,AxMaterial *edited)
{
    for (size_t i = 0; i < planet->count; ++i) {
        const AxVolumeEdit *edit = &planet->edits[i];
        double b = brush_distance(edit, position);
        if (edit->remove) {
            if (-b > field) { field = -b; if (edited) *edited = AX_MATERIAL_ROCK; }
        } else if (b < field) { field = b; if (edited) *edited = edit->material; }
    }
    return field;
}
/** Resolves a position to normalized planet-local meters; returns false for unaddressable coordinates. */
static bool local_point(const AxPosition *position,AxPosition *normalized,double p[3])
{
    *normalized = *position;
    if (!ax_position_normalize(normalized)) return false;
    const AxPosition zero = {0};
    ax_position_difference(normalized, &zero, p);
    return true;
}
/** Evaluates the terrain and edit field; negative means solid. */
static double sample_field(const AxPlanet *planet,const AxPosition *position,bool caves,AxMaterial *material)
{
    if (material) *material = AX_MATERIAL_AIR;
    AxPosition normalized;
    double p[3];
    if (!planet || !position || !local_point(position,&normalized,p)) return INFINITY;
    const AxPlanetDesc *d = &planet->desc;
    AxTerrainClimate climate;
    double field=ax_terrain_density(&planet->terrain,p,caves,material ? &climate : NULL);
    AxMaterial result=AX_MATERIAL_GRASS;
    if (material) {
        double depth=hypot(hypot(p[0],p[1]),p[2])-d->radius-climate.elevation;
        result=depth<-2 ? AX_MATERIAL_ROCK : depth<-0.25 ? AX_MATERIAL_SOIL : AX_MATERIAL_GRASS;
        if (d->terrain_kind==AX_TERRAIN_CONTINENTAL && depth>=-0.25) {
            if (climate.temperature<0.22) result=AX_MATERIAL_SNOW;
            else if (climate.elevation<d->terrain_height*0.025) result=AX_MATERIAL_SAND;
            else if (climate.moisture<0.35) result=AX_MATERIAL_SOIL;
        }
        if (d->surface_material != AX_MATERIAL_AIR) result = d->surface_material;
    }
    field=apply_edits(planet,&normalized,field,&result);
    if (material && field <= 0) *material = result;
    return field;
}
double ax_planet_sample(const AxPlanet *planet,const AxPosition *position,AxMaterial *material)
{
    return sample_field(planet,position,true,material);
}
double ax_planet_sample_lod(const AxPlanet *planet,const AxPosition *position,bool caves)
{
    return sample_field(planet,position,caves,NULL);
}
/** Maps a solid material to its display color, as RGB bytes. */
static void material_color(AxMaterial material,double rgb[3])
{
    static const uint8_t table[AX_MATERIAL_COUNT][3]={{0x77,0x77,0x77},{0x90,0x8b,0x88},{0x8b,0x67,0x42},{0x64,0x93,0x4c},{0x94,0xaf,0xb7},{0xc9,0xb5,0x82},{0xed,0xe8,0xe5}};
    for (size_t k=0;k<3;++k) rgb[k]=table[material][k];
}
/** Blends rgb toward a material color by weight in [0,1]. */
static void blend_material(double rgb[3],AxMaterial material,double weight)
{
    double layer[3];
    material_color(material,layer);
    for (size_t k=0;k<3;++k) rgb[k]+=(layer[k]-rgb[k])*weight;
}
/** Smooth 0-to-1 ramp from a to b; a may exceed b for a falling ramp. */
static double ramp(double a,double b,double x)
{
    double t=fmax(0,fmin(1,(x-a)/(b-a)));
    return t*t*(3-2*t);
}
uint32_t ax_planet_surface_color(const AxPlanet *planet,const AxPosition *position,bool caves,const double normal[3])
{
    AxPosition normalized;
    double p[3],rgb[3];
    material_color(AX_MATERIAL_ROCK,rgb);
    if (planet && position && normal && local_point(position,&normalized,p)) {
        const AxPlanetDesc *d=&planet->desc;
        AxTerrainClimate climate;
        double field=ax_terrain_density(&planet->terrain,p,caves,&climate);
        double length=hypot(hypot(p[0],p[1]),p[2]);
        double depth=length-d->radius-climate.elevation;
        double slope=length>0 ? (normal[0]*p[0]+normal[1]*p[1]+normal[2]*p[2])/length : 1;
        AxMaterial edited=AX_MATERIAL_AIR;
        apply_edits(planet,&normalized,field,&edited);
        if (edited!=AX_MATERIAL_AIR) material_color(edited,rgb);
        else if (field>depth+1e-5) material_color(AX_MATERIAL_ROCK,rgb);
        else if (d->surface_material!=AX_MATERIAL_AIR) material_color(d->surface_material,rgb);
        else {
            material_color(AX_MATERIAL_GRASS,rgb);
            if (d->terrain_kind==AX_TERRAIN_CONTINENTAL) {
                blend_material(rgb,AX_MATERIAL_SOIL,ramp(0.42,0.28,climate.moisture));
                blend_material(rgb,AX_MATERIAL_SAND,ramp(d->terrain_height*0.04,d->terrain_height*0.01,climate.elevation));
                blend_material(rgb,AX_MATERIAL_SNOW,ramp(0.28,0.16,climate.temperature));
            }
            blend_material(rgb,AX_MATERIAL_ROCK,ramp(0.75,0.55,slope));
        }
    }
    return UINT32_C(0xff000000)|(uint32_t)lround(rgb[2])<<16|(uint32_t)lround(rgb[1])<<8|(uint32_t)lround(rgb[0]);
}
bool ax_scene_edit(AxScene *scene, AxPlanetId id, const AxVolumeEdit *edit)
{
    if (!scene || scene->notifying || !edit) return false;
    AxPlanet *planet = (AxPlanet *)ax_scene_planet(scene, id);
    AxVolumeEdit next = *edit;
    if (!planet || planet->count >= AX_PLANET_MAX_EDITS || planet->revision == UINT64_MAX || !ax_position_normalize(&next.position) ||
        (next.shape != AX_BRUSH_SPHERE && next.shape != AX_BRUSH_BOX) ||
        next.material <= AX_MATERIAL_AIR || next.material >= AX_MATERIAL_COUNT) return false;
    for (size_t i = 0; i < 3; ++i)
        if (!isfinite(next.half_extent[i]) || next.half_extent[i] <= 0 || next.half_extent[i] > 1e9) return false;
    if (!reserve((void **)&planet->edits, &planet->capacity, planet->count, sizeof(*planet->edits))) return false;
    planet->edits[planet->count++] = next;
    ++planet->revision;
    changed(scene, id, planet->revision);
    return true;
}
bool ax_planet_up(const AxPosition *position, double up[3])
{
    if (!position || !up) return false;
    AxPosition p = *position;
    if (!ax_position_normalize(&p)) return false;
    const AxPosition zero = {0};
    double delta[3];
    ax_position_difference(&p, &zero, delta);
    double length = hypot(hypot(delta[0], delta[1]), delta[2]);
    if (!(length > 0) || !isfinite(length)) return false;
    for (size_t i = 0; i < 3; ++i) up[i] = delta[i] / length;
    return true;
}
/** Writes a versioned exact floating-point representation without serializing pointers or padding. */
static bool write_scene(FILE *file, const void *user)
{
    const AxScene *scene = user;
    if (fprintf(file, "AXSCENE 3 %zu\n", scene->count) < 0) return false;
    for (size_t i = 0; i < scene->count; ++i) {
        const AxPlanet *p = scene->planets[i];
        const AxPlanetDesc *d = &p->desc;
        if (fprintf(file, "P %" PRIu64 " %" PRIu64 " %a %a %a %a %a %a %u %u %a %a %zu\n", d->id, d->seed,
                    d->center[0], d->center[1], d->center[2], d->radius, d->terrain_height, d->terrain_frequency, (unsigned)d->surface_material, (unsigned)d->terrain_kind, d->surface_gravity, d->cave_depth, p->count) < 0) return false;
        for (size_t j = 0; j < p->count; ++j) {
            const AxVolumeEdit *e = &p->edits[j];
            if (fprintf(file, "E %d %d %d %" PRId64 " %" PRId64 " %" PRId64 " %a %a %a %a %a %a\n",
                        (int)e->shape, (int)e->material, (int)e->remove, e->position.chunk[0], e->position.chunk[1], e->position.chunk[2],
                        e->position.offset[0], e->position.offset[1], e->position.offset[2],
                        e->half_extent[0], e->half_extent[1], e->half_extent[2]) < 0) return false;
        }
    }
    return !ferror(file);
}
bool ax_scene_save(const AxScene *scene, const char *path)
{
    return scene && path && path[0] && ax_file_write_atomic(path, write_scene, scene);
}
/** Reads one bounded record, rejecting excess fields, embedded NULs, and overlong lines. */
static size_t read_record(FILE *file, char line[1024], char *fields[16])
{
    size_t length = 0;
    int c;
    while ((c = fgetc(file)) != EOF && c != '\n') {
        if (!c || length == 1023) return 0;
        line[length++] = (char)c;
    }
    line[length] = 0;
    size_t count = 0;
    char *p = line;
    while (*p) {
        while (isspace((unsigned char)*p)) ++p;
        if (!*p) break;
        if (count == 16) return 0;
        fields[count++] = p;
        while (*p && !isspace((unsigned char)*p)) ++p;
        if (*p) *p++ = 0;
    }
    return count;
}
/** Parses an unsigned decimal token with exact range checking. */
static bool parse_unsigned(const char *text, uint64_t *value)
{
    if (!text || !isdigit((unsigned char)*text)) return false;
    char *end;
    errno = 0;
    uintmax_t n = strtoumax(text, &end, 10);
    if (errno || *end || n > UINT64_MAX) return false;
    *value = (uint64_t)n;
    return true;
}
/** Parses a signed decimal chunk address without overflowing scanf conversions. */
static bool parse_signed(const char *text, int64_t *value)
{
    char *end;
    errno = 0;
    intmax_t n = strtoimax(text, &end, 10);
    if (errno || end == text || *end || n < INT64_MIN || n > INT64_MAX) return false;
    *value = (int64_t)n;
    return true;
}
/** Parses a finite exact floating-point token. */
static bool parse_real(const char *text, double *value)
{
    char *end;
    errno = 0;
    *value = strtod(text, &end);
    return !errno && end != text && !*end && isfinite(*value);
}
bool ax_scene_load(AxScene *scene, const char *path)
{
    if (!scene || scene->notifying || !path || scene->epoch==UINT64_MAX) return false;
    FILE *file = ax_file_open(path, "rb");
    if (!file) return false;
    AxScene next = {0};
    char line[1024], *fields[16];
    uint64_t planets = 0;
    uint64_t version=0;
    bool valid = read_record(file, line, fields) == 3 && !strcmp(fields[0], "AXSCENE") &&
                 (parse_unsigned(fields[1],&version) && version>=1 && version<=3) && parse_unsigned(fields[2], &planets) && planets <= AX_SCENE_MAX_PLANETS;
    for (uint64_t i = 0; valid && i < planets; ++i) {
        AxPlanetDesc d = {0};
        uint64_t edits = 0;
        valid = read_record(file, line, fields) == (version==3 ? 14u : version==2 ? 11u : 10u) && !strcmp(fields[0], "P") &&
                parse_unsigned(fields[1], &d.id) && parse_unsigned(fields[2], &d.seed);
        for (size_t k = 0; valid && k < 3; ++k) valid = parse_real(fields[3 + k], &d.center[k]);
        uint64_t surface = 0;
        valid = valid && (version<2 || (parse_unsigned(fields[9], &surface) && surface < AX_MATERIAL_COUNT));
        d.surface_material = (AxMaterial)surface;
        uint64_t kind=0;
        if (version==3) {
            valid=valid && parse_unsigned(fields[10],&kind) && kind<AX_TERRAIN_COUNT &&
                  parse_real(fields[11],&d.surface_gravity) && parse_real(fields[12],&d.cave_depth);
            d.terrain_kind=(AxTerrainKind)kind;
        } else d.surface_gravity=9.81;
        valid = valid && parse_real(fields[6], &d.radius) && parse_real(fields[7], &d.terrain_height) &&
                parse_real(fields[8], &d.terrain_frequency) && parse_unsigned(fields[version==3 ? 13 : version==2 ? 10 : 9], &edits) && edits <= AX_PLANET_MAX_EDITS;
        if (!valid || !ax_scene_add_planet(&next, &d)) { valid = false; break; }
        for (uint64_t j = 0; valid && j < edits; ++j) {
            AxVolumeEdit e = {0};
            uint64_t shape, material, remove;
            valid = read_record(file, line, fields) == 13 && !strcmp(fields[0], "E") &&
                    parse_unsigned(fields[1], &shape) && shape <= AX_BRUSH_BOX &&
                    parse_unsigned(fields[2], &material) && material < AX_MATERIAL_COUNT &&
                    parse_unsigned(fields[3], &remove) && remove <= 1;
            for (size_t k = 0; valid && k < 3; ++k)
                valid = parse_signed(fields[4 + k], &e.position.chunk[k]) &&
                        parse_real(fields[7 + k], &e.position.offset[k]) && parse_real(fields[10 + k], &e.half_extent[k]);
            if (!valid) break;
            e.shape = (AxBrushShape)shape; e.material = (AxMaterial)material; e.remove = remove != 0;
            valid = ax_scene_edit(&next, d.id, &e);
        }
    }
    int trailing;
    while (valid && (trailing = fgetc(file)) != EOF) if (!isspace((unsigned char)trailing)) valid = false;
    if (ferror(file)) valid = false;
    if (fclose(file) != 0) valid = false;
    if (valid) {
        AxEvents *events = scene->events;
        next.epoch=scene->epoch+1;
        ax_scene_destroy(scene);
        *scene = next;
        scene->events = events;
        changed(scene, 0, 0);
    } else ax_scene_destroy(&next);
    return valid;
}
/** Deep-copies immutable planet state without copying any allocation ownership. */
static AxPlanet *clone_planet(const AxPlanet *source)
{
    AxPlanet *planet=malloc(sizeof(*planet));
    if (!planet) return NULL;
    *planet=*source;
    planet->edits=NULL;
    planet->capacity=planet->count;
    if (source->count) {
        planet->edits=malloc(source->count*sizeof(*planet->edits));
        if (!planet->edits) { free(planet); return NULL; }
        memcpy(planet->edits,source->edits,source->count*sizeof(*planet->edits));
    }
    return planet;
}
bool ax_scene_copy_planet(const AxScene *scene, AxPlanetId id, AxScene *copy)
{
    const AxPlanet *source=ax_scene_planet(scene,id);
    if (!source || !copy || copy->count || copy->planets || copy->notifying) return false;
    AxScene next={0};
    next.planets=calloc(1,sizeof(*next.planets));
    if (!next.planets) return false;
    next.planets[0]=clone_planet(source);
    if (!next.planets[0]) { free(next.planets); return false; }
    next.count=next.capacity=1;
    next.epoch=scene->epoch;
    *copy=next;
    return true;
}
double ax_planet_extent(const AxPlanet *planet)
{
    double half = planet->desc.radius + planet->desc.terrain_height + 1;
    const AxPosition zero = {0};
    for (size_t i = 0; i < planet->count; ++i) if (!planet->edits[i].remove) {
        double p[3];
        ax_position_difference(&planet->edits[i].position, &zero, p);
        for (size_t k = 0; k < 3; ++k) half = fmax(half, fabs(p[k]) + planet->edits[i].half_extent[planet->edits[i].shape == AX_BRUSH_SPHERE ? 0 : k] + 1);
    }
    return exp2(ceil(log2(half * 2)));
}
const char *ax_planet_name(AxPlanetId id)
{
    return id == 1 ? "Terra" : id == 2 ? "Luna" : "Planet";
}

bool ax_scene_clone(const AxScene *scene, AxScene *copy)
{
    if (!scene || !copy || scene==copy || copy->notifying) return false;
    AxScene next={0};
    if (scene->count) {
        next.planets=calloc(scene->count,sizeof(*next.planets));
        if (!next.planets) return false;
        next.capacity=scene->count;
    }
    for (size_t i=0; i<scene->count; ++i) {
        AxPlanet *planet=clone_planet(scene->planets[i]);
        if (!planet) { ax_scene_destroy(&next); return false; }
        next.planets[next.count++]=planet;
    }
    next.epoch=scene->epoch;
    ax_scene_destroy(copy);
    *copy=next;
    return true;
}

bool ax_planet_density_bounds(const AxPlanet *planet,const AxPosition *origin,double extent,bool caves,double *minimum,double *maximum)
{
    if (!planet || !origin || !minimum || !maximum || !isfinite(extent) || extent<=0) return false;
    AxPosition center=*origin;
    if (!ax_position_move(&center,(double[3]){extent/2,extent/2,extent/2})) return false;
    const AxPosition zero={0};
    double p[3];
    ax_position_difference(&center,&zero,p);
    double radius=hypot(hypot(p[0],p[1]),p[2]),reach=extent*0.8660254037844387;
    const AxPlanetDesc *d=&planet->desc;
    double lo=fmax(0,radius-reach)-d->radius-d->terrain_height;
    double hi=radius+reach-d->radius+d->terrain_height;
    if (caves && d->cave_depth>0) {
        double cave_max=fmin(-lo-6,hi+d->cave_depth);
        hi=fmax(hi,cave_max);
    }
    for (size_t i=0;i<planet->count;++i) {
        const AxVolumeEdit *edit=&planet->edits[i];
        double distance=brush_distance(edit,&center),a=distance-reach,b=distance+reach;
        if (edit->remove) { lo=fmax(lo,-b); hi=fmax(hi,-a); }
        else { lo=fmin(lo,a); hi=fmin(hi,b); }
    }
    *minimum=lo; *maximum=hi;
    return true;
}

bool ax_planet_surface_likely(const AxPlanet *planet,const AxPosition *origin,double extent,bool caves)
{
    if (!planet || !origin || !isfinite(extent) || extent<=0) return false;
    AxPosition center=*origin;
    if (!ax_position_move(&center,(double[3]){extent/2,extent/2,extent/2})) return false;
    const AxPosition zero={0};
    double p[3];
    ax_position_difference(&center,&zero,p);
    double reach=extent*0.8660254037844387;
    for (size_t i=0;i<planet->count;++i) if (fabs(brush_distance(&planet->edits[i],&center))<=reach) return true;
    double lowest=INFINITY,highest=-INFINITY;
    for (int z=-1;z<=1;z+=2) for (int y=-1;y<=1;y+=2) for (int x=-1;x<=1;x+=2) {
        double corner[3]={p[0]+x*extent/2,p[1]+y*extent/2,p[2]+z*extent/2};
        double height=ax_terrain_height(&planet->terrain,corner,NULL);
        lowest=fmin(lowest,height); highest=fmax(highest,height);
    }
    double height=ax_terrain_height(&planet->terrain,p,NULL);
    lowest=fmin(lowest,height); highest=fmax(highest,height);
    const AxPlanetDesc *d=&planet->desc;
    double radius=hypot(hypot(p[0],p[1]),p[2]),margin=extent+(highest-lowest)*0.5;
    double band=caves && d->cave_depth>0 ? d->cave_depth+fmax(8,d->cave_depth*0.25) : 0;
    return radius-reach-d->radius-highest<=margin && radius+reach-d->radius-lowest>=-band-margin;
}

double ax_planet_terrain_height(const AxPlanet *planet,const double position[3])
{
    return planet && position ? ax_terrain_height(&planet->terrain,position,NULL) : 0;
}
