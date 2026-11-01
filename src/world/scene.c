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
    double phase[3];
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
/** Maps a seed to reproducible integer randomness without shared RNG state. */
static uint64_t mix(uint64_t value)
{
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}
/** Validates a finite generation domain with centimeter-scale double precision. */
static bool valid_description(const AxPlanetDesc *d)
{
    if (!d || !d->id || !isfinite(d->radius) || d->radius < 1 || d->radius > 1e12 ||
        !isfinite(d->terrain_height) || d->terrain_height < 0 || d->terrain_height > d->radius * 0.25 ||
        !isfinite(d->terrain_frequency) || d->terrain_frequency < 0 || d->terrain_frequency > 16 || d->surface_material < AX_MATERIAL_AIR || d->surface_material >= AX_MATERIAL_COUNT) return false;
    for (size_t i = 0; i < 3; ++i) if (!isfinite(d->center[i])) return false;
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
    if (!reserve((void **)&scene->planets, &scene->capacity, scene->count, sizeof(*scene->planets))) { free(planet); return NULL; }
    planet->desc = *desc;
    planet->revision = 1;
    for (size_t i = 0; i < 3; ++i) planet->phase[i] = (double)(mix(desc->seed + i) >> 11) * 0x1p-53 * 6.283185307179586;
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
double ax_planet_sample(const AxPlanet *planet, const AxPosition *position, AxMaterial *material)
{
    if (material) *material = AX_MATERIAL_AIR;
    if (!planet || !position) return INFINITY;
    AxPosition normalized = *position;
    if (!ax_position_normalize(&normalized)) return INFINITY;
    double p[3];
    const AxPosition zero = {0};
    ax_position_difference(&normalized, &zero, p);
    const AxPlanetDesc *d = &planet->desc;
    double height = d->terrain_height;
    for (size_t i = 0; i < 3; ++i) height *= sin(p[i] * d->terrain_frequency + planet->phase[i]);
    double field = hypot(hypot(p[0], p[1]), p[2]) - d->radius - height;
    AxMaterial result = field < -2 ? AX_MATERIAL_ROCK : field < -0.25 ? AX_MATERIAL_SOIL : AX_MATERIAL_GRASS;
    if (d->surface_material != AX_MATERIAL_AIR) result = d->surface_material;
    for (size_t i = 0; i < planet->count; ++i) {
        const AxVolumeEdit *edit = &planet->edits[i];
        double b = brush_distance(edit, &normalized);
        if (edit->remove) {
            if (-b > field) { field = -b; result = AX_MATERIAL_ROCK; }
        } else if (b < field) { field = b; result = edit->material; }
    }
    if (material && field <= 0) *material = result;
    return field;
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
    if (fprintf(file, "AXSCENE 2 %zu\n", scene->count) < 0) return false;
    for (size_t i = 0; i < scene->count; ++i) {
        const AxPlanet *p = scene->planets[i];
        const AxPlanetDesc *d = &p->desc;
        if (fprintf(file, "P %" PRIu64 " %" PRIu64 " %a %a %a %a %a %a %u %zu\n", d->id, d->seed,
                    d->center[0], d->center[1], d->center[2], d->radius, d->terrain_height, d->terrain_frequency, (unsigned)d->surface_material, p->count) < 0) return false;
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
    if (!scene || scene->notifying || !path) return false;
    FILE *file = ax_file_open(path, "rb");
    if (!file) return false;
    AxScene next = {0};
    char line[1024], *fields[16];
    uint64_t planets = 0;
    bool version_two = false;
    bool valid = read_record(file, line, fields) == 3 && !strcmp(fields[0], "AXSCENE") &&
                 (!strcmp(fields[1], "1") || (version_two = !strcmp(fields[1], "2"))) && parse_unsigned(fields[2], &planets) && planets <= AX_SCENE_MAX_PLANETS;
    for (uint64_t i = 0; valid && i < planets; ++i) {
        AxPlanetDesc d = {0};
        uint64_t edits = 0;
        valid = read_record(file, line, fields) == (version_two ? 11u : 10u) && !strcmp(fields[0], "P") &&
                parse_unsigned(fields[1], &d.id) && parse_unsigned(fields[2], &d.seed);
        for (size_t k = 0; valid && k < 3; ++k) valid = parse_real(fields[3 + k], &d.center[k]);
        uint64_t surface = 0;
        valid = valid && (!version_two || (parse_unsigned(fields[9], &surface) && surface < AX_MATERIAL_COUNT));
        d.surface_material = (AxMaterial)surface;
        valid = valid && parse_real(fields[6], &d.radius) && parse_real(fields[7], &d.terrain_height) &&
                parse_real(fields[8], &d.terrain_frequency) && parse_unsigned(fields[version_two ? 10 : 9], &edits) && edits <= AX_PLANET_MAX_EDITS;
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
    ax_scene_destroy(copy);
    *copy=next;
    return true;
}
