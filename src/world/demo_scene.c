#include "demo_scene.h"
#include <math.h>
#include <stdlib.h>
#define SCENE_EXTENT 64.0f
#define SCENE_DEPTH 10u
#define WATER_LEVEL 5.0f
#define TERRAIN_NORMALIZE 0.36f
#define CLUMP_SCALE 0.25f
#define CLUMP_RAGGED 0.24f
#define TERRAIN_MIN WATER_LEVEL
#define TERRAIN_MAX 10.5f
#define TREE_COUNT 30
#define TREE_CLUMPS 6
#define BUCKETS 16
#define BUCKET_TREES 24
#define BUCKET_REACH 2.0f
#define SPAWN_X 32.0f
#define SPAWN_Z 60.0f
#define RUIN_X 15.5f
#define RUIN_Z 43.0f
typedef enum { MAT_GROUND, MAT_BARK, MAT_LEAVES, MAT_RUIN } Material;
typedef struct { float a[3], b[3], radius; } Capsule;
typedef struct { float center[3], radius; } Clump;
typedef struct {
    Capsule limbs[TREE_CLUMPS + 2];
    Clump clumps[TREE_CLUMPS];
    uint32_t limb_count, clump_count;
    float bound_center[3], bound_radius, crown_bottom, crown_top;
} Tree;
typedef struct {
    float ruin_base;
    Tree trees[TREE_COUNT];
    uint32_t tree_count;
    uint8_t buckets[BUCKETS][BUCKETS][BUCKET_TREES];
    uint32_t bucket_count[BUCKETS][BUCKETS];
} Scene;
/** Packs an opaque RGB color as ABGR. */
static uint32_t rgb(uint32_t r, uint32_t g, uint32_t b) { return 0xff000000u | (b << 16) | (g << 8) | r; }
/** Scales an ABGR color's channels by a factor, saturating. */
static uint32_t shade(uint32_t color, float factor)
{
    uint32_t out = 0xff000000u;
    for (int c = 0; c < 3; ++c) {
        float v = (float)((color >> (8 * c)) & 0xffu) * factor;
        out |= (uint32_t)(v > 255.0f ? 255.0f : v < 0.0f ? 0.0f : v) << (8 * c);
    }
    return out;
}
/** Blends two ABGR colors. */
static uint32_t mix_color(uint32_t a, uint32_t b, float t)
{
    uint32_t out = 0xff000000u;
    for (int c = 0; c < 3; ++c) {
        float va = (float)((a >> (8 * c)) & 0xffu), vb = (float)((b >> (8 * c)) & 0xffu);
        out |= (uint32_t)(va + (vb - va) * t) << (8 * c);
    }
    return out;
}
/** Integer hash for deterministic per-voxel variation. */
static uint32_t hash3(int32_t x, int32_t y, int32_t z)
{
    uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u ^ (uint32_t)z * 0xcb1ab31fu;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}
/** Returns a hash mapped into [0, 1). */
static float random01(int32_t x, int32_t y, int32_t z) { return (float)(hash3(x, y, z) & 0xffffffu) / 16777216.0f; }
/** Smoothstep-interpolated 3D value noise in [0, 1); gradient magnitude is at most 1.5 * sqrt(3). */
static float noise3(float x, float y, float z)
{
    float fx = floorf(x), fy = floorf(y), fz = floorf(z);
    int32_t ix = (int32_t)fx, iy = (int32_t)fy, iz = (int32_t)fz;
    float t[3] = {x - fx, y - fy, z - fz};
    for (int i = 0; i < 3; ++i) t[i] = t[i] * t[i] * (3.0f - 2.0f * t[i]);
    float result = 0.0f;
    for (uint32_t k = 0; k < 8; ++k) {
        float w = ((k & 1u) ? t[0] : 1.0f - t[0]) * ((k & 2u) ? t[1] : 1.0f - t[1]) * ((k & 4u) ? t[2] : 1.0f - t[2]);
        result += w * random01(ix + (int32_t)(k & 1u), iy + (int32_t)((k >> 1) & 1u), iz + (int32_t)((k >> 2) & 1u));
    }
    return result;
}
/** Smoothstep-interpolated 2D value noise in [0, 1); gradient magnitude is at most 1.5 * sqrt(2). */
static float noise2(float x, float z, int32_t seed)
{
    float fx = floorf(x), fz = floorf(z);
    int32_t ix = (int32_t)fx, iz = (int32_t)fz;
    float tx = x - fx, tz = z - fz;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    float a = random01(ix, seed, iz), b = random01(ix + 1, seed, iz);
    float c = random01(ix, seed, iz + 1), d = random01(ix + 1, seed, iz + 1);
    float top = a + (b - a) * tx, bottom = c + (d - c) * tx;
    return top + (bottom - top) * tz;
}
/** Terrain height in meters: rolling hills with a basin that holds the pond. Slope stays below 2.6. */
static float terrain_height(float x, float z)
{
    float height = 3.0f, amplitude = 4.0f, frequency = 1.0f / 16.0f;
    for (int octave = 0; octave < 4; ++octave, amplitude *= 0.5f, frequency *= 2.0f)
        height += noise2(x * frequency, z * frequency, octave) * amplitude;
    float dx = x - 37.5f, dz = z - 30.0f;
    return height - 4.0f * expf(-(dx * dx + dz * dz) / (2.0f * 5.5f * 5.5f));
}
/** Exact capsule distance. */
static float capsule(const Capsule *c, const float p[3])
{
    float ab[3], ap[3];
    for (int i = 0; i < 3; ++i) { ab[i] = c->b[i] - c->a[i]; ap[i] = p[i] - c->a[i]; }
    float t = (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / (ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2]);
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    float d[3] = {ap[0] - ab[0] * t, ap[1] - ab[1] * t, ap[2] - ab[2] * t};
    return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - c->radius;
}
/**
 * Ragged foliage clump: max(k * (sphere + ragged), sphere - A) with |ragged| <= A and k * (1 + |grad ragged|) <= 1.
 * Both terms are 1-Lipschitz and the zero set is that of the displaced sphere; away from the displacement shell
 * the second term is exact and the noise is skipped.
 */
static float clump(const Clump *c, const float p[3])
{
    float d[3] = {p[0] - c->center[0], p[1] - c->center[1], p[2] - c->center[2]};
    float sphere = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - c->radius;
    if (sphere >= CLUMP_RAGGED * (1.0f + CLUMP_SCALE) / (1.0f - CLUMP_SCALE)) return sphere - CLUMP_RAGGED;
    float ragged = (noise3(p[0] * 1.8f, p[1] * 1.8f, p[2] * 1.8f) - 0.5f) * 0.4f + (noise3(p[0] * 5.0f, p[1] * 5.0f, p[2] * 5.0f) - 0.5f) * 0.08f;
    return fmaxf((sphere + ragged) * CLUMP_SCALE, sphere - CLUMP_RAGGED);
}
/** Hollow crumbling tower with window cut-outs, scaled to stay 1-Lipschitz. */
static float ruin(const Scene *scene, const float p[3])
{
    float dx = p[0] - RUIN_X, dz = p[2] - RUIN_Z, radius = sqrtf(dx * dx + dz * dz);
    float bound = radius - 2.6f;
    if (bound > 1.0f) return bound;
    float angle = atan2f(dz, dx);
    float top = 3.6f + (sinf(angle * 3.0f) * 1.1f + (noise3(dx * 1.5f, 3.0f, dz * 1.5f) - 0.5f) * 0.8f) * fminf(radius * 0.5f, 1.0f);
    float wall = fabsf(radius - 2.2f) - 0.25f;
    float base = scene->ruin_base;
    float shape = fmaxf(wall, fmaxf((p[1] - base - top) * 0.3f, base - p[1]));
    float wx = fabsf(fmodf(angle + 3.14159265f, 1.5707963f) - 0.785f) * radius - 0.3f;
    float wy = fabsf(p[1] - base - 2.4f) - 0.45f;
    float window = fmaxf(wx, wy) * 0.45f;
    return fmaxf(shape, -window);
}
/** Signed distance of the whole scene and the material of the closest surface. */
static float scene_eval(const Scene *scene, const float p[3], Material *material, const Tree **tree)
{
    float ground;
    if (p[1] > TERRAIN_MAX + 1.0f) ground = p[1] - TERRAIN_MAX;
    else if (p[1] < TERRAIN_MIN - 1.0f) ground = p[1] - TERRAIN_MIN;
    else ground = fmaxf(fminf((p[1] - fmaxf(terrain_height(p[0], p[2]), WATER_LEVEL)) * TERRAIN_NORMALIZE, p[1] - TERRAIN_MIN), p[1] - TERRAIN_MAX);
    float best = ground;
    Material kind = MAT_GROUND;
    const Tree *nearest = NULL;
    float r = ruin(scene, p);
    if (r < best) { best = r; kind = MAT_RUIN; }
    int bx = (int)floorf(p[0] / (SCENE_EXTENT / BUCKETS)), bz = (int)floorf(p[2] / (SCENE_EXTENT / BUCKETS));
    bx = bx < 0 ? 0 : bx >= BUCKETS ? BUCKETS - 1 : bx;
    bz = bz < 0 ? 0 : bz >= BUCKETS ? BUCKETS - 1 : bz;
    float unlisted = BUCKET_REACH;
    if (p[0] < 0.0f || p[0] > SCENE_EXTENT || p[2] < 0.0f || p[2] > SCENE_EXTENT) unlisted = 0.0f;
    for (uint32_t i = 0; i < scene->bucket_count[bx][bz]; ++i) {
        const Tree *t = &scene->trees[scene->buckets[bx][bz][i]];
        float d[3] = {p[0] - t->bound_center[0], p[1] - t->bound_center[1], p[2] - t->bound_center[2]};
        if (sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - t->bound_radius > best) continue;
        for (uint32_t j = 0; j < t->limb_count; ++j) {
            float v = capsule(&t->limbs[j], p);
            if (v < best) { best = v; kind = MAT_BARK; nearest = t; }
        }
        for (uint32_t j = 0; j < t->clump_count; ++j) {
            float v = clump(&t->clumps[j], p);
            if (v < best) { best = v; kind = MAT_LEAVES; nearest = t; }
        }
    }
    if (unlisted > 0.0f && unlisted < best) best = unlisted;
    if (material) *material = kind;
    if (tree) *tree = nearest;
    return best;
}
/** Source callback: scene signed distance. */
static float scene_distance(const void *user, const float p[3]) { return scene_eval(user, p, NULL, NULL); }
/** Source callback: per-voxel material color with tiny-voxel jitter. */
static uint32_t scene_color(const void *user, const float p[3], const float n[3], const int32_t cell[3])
{
    Material material;
    const Tree *tree;
    scene_eval(user, p, &material, &tree);
    float jitter = 0.9f + random01(cell[0], cell[1], cell[2]) * 0.2f;
    uint32_t h = hash3(cell[0], cell[1] + 101, cell[2]);
    switch (material) {
    case MAT_BARK: {
        float stripe = 0.85f + 0.15f * sinf(atan2f(n[2], n[0]) * 7.0f);
        return shade(rgb(150, 106, 74), jitter * stripe);
    }
    case MAT_LEAVES: {
        const uint32_t ramp[] = {rgb(60, 62, 118), rgb(56, 92, 110), rgb(56, 114, 74), rgb(74, 138, 70),
            rgb(100, 160, 72), rgb(132, 182, 82), rgb(176, 210, 106), rgb(218, 230, 142)};
        float t = tree ? (p[1] - tree->crown_bottom) / (tree->crown_top - tree->crown_bottom) : 0.5f;
        t += (random01(cell[0], cell[1] + 7, cell[2]) - 0.5f) * 0.3f + n[1] * 0.12f;
        t = t < 0.0f ? 0.0f : t > 0.999f ? 0.999f : t;
        float scaled = t * 7.0f;
        int index = (int)scaled;
        return shade(mix_color(ramp[index], ramp[index + 1], scaled - (float)index), jitter);
    }
    case MAT_RUIN: {
        const Scene *scene = user;
        float height = p[1] - scene->ruin_base;
        float along = atan2f(p[2] - RUIN_Z, p[0] - RUIN_X) * 2.2f + (fmodf(floorf(height / 0.25f), 2.0f) ? 0.25f : 0.0f);
        bool mortar = fmodf(height, 0.25f) < 0.05f || fmodf(along + 100.0f, 0.5f) < 0.05f;
        uint32_t stone = h % 5 == 0 ? rgb(150, 142, 128) : rgb(172, 162, 148);
        if (height < 1.2f && noise3(p[0] * 3.0f, p[1] * 3.0f, p[2] * 3.0f) > 0.55f) stone = rgb(112, 138, 90);
        return shade(mortar ? rgb(120, 114, 106) : stone, jitter);
    }
    case MAT_GROUND: break;
    }
    float height = terrain_height(p[0], p[2]);
    if (height < WATER_LEVEL - 0.02f) {
        float depth = fminf((WATER_LEVEL - height) / 2.5f, 1.0f);
        return shade(mix_color(rgb(84, 150, 170), rgb(40, 88, 140), depth), 0.95f + random01(cell[0], 0, cell[2]) * 0.08f);
    }
    if (height < WATER_LEVEL + 0.3f) return shade(rgb(214, 198, 148), jitter);
    if (n[1] < 0.55f) return shade(n[1] < 0.35f ? rgb(126, 124, 120) : rgb(124, 92, 64), jitter);
    if (h % 160 == 0) { const uint32_t petals[] = {rgb(226, 86, 86), rgb(242, 210, 88), rgb(240, 238, 230)}; return petals[(h >> 8) % 3]; }
    return shade(mix_color(rgb(86, 140, 58), rgb(122, 172, 74), noise3(p[0] * 0.4f, 0.0f, p[2] * 0.4f)), jitter);
}
/** Grows one stylized tree: a trunk splitting into branches that end in foliage clumps. */
static void grow_tree(Tree *tree, float x, float z, uint32_t seed)
{
    float ground = terrain_height(x, z), trunk = 3.6f + random01((int32_t)seed, 1, 1) * 1.8f, top = ground + trunk;
    *tree = (Tree){.limb_count = 1, .crown_bottom = top - 0.6f, .crown_top = top + 3.4f};
    tree->limbs[0] = (Capsule){{x, ground - 0.5f, z}, {x, top, z}, 0.2f};
    uint32_t branches = 3 + seed % 3;
    float lo[3] = {x, top, z}, hi[3] = {x, top, z};
    for (uint32_t i = 0; i < branches; ++i) {
        float angle = (float)i / (float)branches * 6.2831853f + random01((int32_t)seed, (int32_t)i, 3) * 1.2f;
        float reach = 1.1f + random01((int32_t)seed, (int32_t)i, 5) * 0.9f;
        float tip[3] = {x + cosf(angle) * reach, top + 0.8f + random01((int32_t)seed, (int32_t)i, 7) * 1.1f, z + sinf(angle) * reach};
        tree->limbs[tree->limb_count++] = (Capsule){{x, top - 0.7f, z}, {tip[0], tip[1], tip[2]}, 0.09f};
        float radius = 1.0f + random01((int32_t)seed, (int32_t)i, 11) * 0.5f;
        tree->clumps[tree->clump_count++] = (Clump){{tip[0], tip[1] + 0.3f, tip[2]}, radius};
    }
    tree->clumps[tree->clump_count++] = (Clump){{x, top + 1.9f, z}, 1.4f + random01((int32_t)seed, 9, 9) * 0.4f};
    for (uint32_t i = 0; i < tree->clump_count; ++i)
        for (int a = 0; a < 3; ++a) {
            lo[a] = fminf(lo[a], tree->clumps[i].center[a] - tree->clumps[i].radius - 0.6f);
            hi[a] = fmaxf(hi[a], tree->clumps[i].center[a] + tree->clumps[i].radius + 0.6f);
        }
    lo[1] = ground - 0.5f;
    float extent[3];
    for (int a = 0; a < 3; ++a) { tree->bound_center[a] = (lo[a] + hi[a]) * 0.5f; extent[a] = (hi[a] - lo[a]) * 0.5f; }
    tree->bound_radius = sqrtf(extent[0] * extent[0] + extent[1] * extent[1] + extent[2] * extent[2]);
}
/** Scatters trees on dry ground away from the ruin and spawn, then indexes them into XZ buckets. */
static bool plant_trees(Scene *scene)
{
    for (uint32_t attempt = 0; attempt < 4096 && scene->tree_count < TREE_COUNT; ++attempt) {
        float x = 4.0f + random01((int32_t)attempt, 1, 2) * (SCENE_EXTENT - 8.0f);
        float z = 4.0f + random01((int32_t)attempt, 3, 4) * (SCENE_EXTENT - 8.0f);
        if (terrain_height(x, z) <= WATER_LEVEL + 0.6f) continue;
        if (hypotf(x - RUIN_X, z - RUIN_Z) < 6.0f || hypotf(x - SPAWN_X, z - SPAWN_Z) < 8.0f) continue;
        bool crowded = false;
        for (uint32_t i = 0; i < scene->tree_count && !crowded; ++i)
            crowded = hypotf(scene->trees[i].limbs[0].a[0] - x, scene->trees[i].limbs[0].a[2] - z) < 4.5f;
        if (crowded) continue;
        grow_tree(&scene->trees[scene->tree_count++], x, z, hash3((int32_t)(x * 16.0f), 5, (int32_t)(z * 16.0f)));
    }
    const float cell = SCENE_EXTENT / BUCKETS;
    for (uint32_t t = 0; t < scene->tree_count; ++t) {
        const Tree *tree = &scene->trees[t];
        for (int bx = 0; bx < BUCKETS; ++bx)
            for (int bz = 0; bz < BUCKETS; ++bz) {
                float nx = fmaxf((float)bx * cell, fminf(tree->bound_center[0], (float)(bx + 1) * cell));
                float nz = fmaxf((float)bz * cell, fminf(tree->bound_center[2], (float)(bz + 1) * cell));
                if (hypotf(nx - tree->bound_center[0], nz - tree->bound_center[2]) > tree->bound_radius + BUCKET_REACH) continue;
                if (scene->bucket_count[bx][bz] == BUCKET_TREES) return false;
                scene->buckets[bx][bz][scene->bucket_count[bx][bz]++] = (uint8_t)t;
            }
    }
    return true;
}
bool ax_demo_scene_build(AxOctree *octree, AxCamera *camera, AxJobs *jobs)
{
    if (!octree) return false;
    *octree = (AxOctree){0};
    Scene *scene = calloc(1, sizeof(*scene));
    if (!scene) return false;
    scene->ruin_base = terrain_height(RUIN_X, RUIN_Z) - 1.5f;
    AxVoxelSource source = {scene_distance, scene_color, scene, SCENE_EXTENT, SCENE_DEPTH, 1.0f};
    bool built = plant_trees(scene) && ax_octree_build(octree, &source, jobs);
    free(scene);
    if (built && camera) {
        const float spawn[3] = {SPAWN_X, fmaxf(terrain_height(SPAWN_X, SPAWN_Z), WATER_LEVEL) + 2.5f, SPAWN_Z};
        ax_camera_init(camera, spawn, 0.0f, -0.2f);
    }
    return built;
}
