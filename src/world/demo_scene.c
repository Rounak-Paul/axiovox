#include "demo_scene.h"
#include <math.h>
#define SCENE_SIZE 256u
#define WATER_LEVEL 34
#define TREE_COUNT 34
#define SPAWN_X 128
#define SPAWN_Z 244
enum {
    MAT_GRASS = 1, MAT_DIRT = 5, MAT_STONE = 8, MAT_DEEP_STONE = 12, MAT_SAND = 13, MAT_WATER = 15,
    MAT_BARK = 17, MAT_LEAVES = 19, MAT_FLOWER = 27, MAT_BRICK = 30, MAT_MOSS_BRICK = 32
};
#define LEAF_SHADES 8
/** Packs an opaque RGB color as ABGR. */
static uint32_t rgb(uint32_t r, uint32_t g, uint32_t b) { return 0xff000000u | (b << 16) | (g << 8) | r; }
/** Integer hash for deterministic per-voxel variation. */
static uint32_t hash3(int32_t x, int32_t y, int32_t z)
{
    uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u ^ (uint32_t)z * 0xcb1ab31fu;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}
/** Returns a hash mapped into [0, 1). */
static float random01(int32_t x, int32_t y, int32_t z) { return (float)(hash3(x, y, z) & 0xffffffu) / 16777216.0f; }
/** Smoothly interpolated 2D value noise in [0, 1). */
static float value_noise(float x, float z, int32_t seed)
{
    float fx = floorf(x), fz = floorf(z);
    int32_t ix = (int32_t)fx, iz = (int32_t)fz;
    float tx = x - fx, tz = z - fz;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    float a = random01(ix, seed, iz), b = random01(ix + 1, seed, iz);
    float c = random01(ix, seed, iz + 1), d = random01(ix + 1, seed, iz + 1);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
}
/** Terrain height in voxels: rolling hills with a central basin for the pond. */
static int terrain_height(int x, int z)
{
    float height = 0.0f, amplitude = 18.0f, frequency = 1.0f / 64.0f;
    for (int octave = 0; octave < 4; ++octave, amplitude *= 0.5f, frequency *= 2.0f)
        height += value_noise((float)x * frequency, (float)z * frequency, octave) * amplitude;
    float dx = (float)x - 150.0f, dz = (float)z - 120.0f;
    float basin = expf(-(dx * dx + dz * dz) / (2.0f * 22.0f * 22.0f)) * 16.0f;
    return 28 + (int)height - (int)basin;
}
/** Fills the palette with jittered material families and a shadow-to-sunlit leaf ramp. */
static void fill_palette(AxVoxelGrid *grid)
{
    const uint32_t grass[] = {rgb(94, 148, 62), rgb(104, 160, 66), rgb(86, 138, 58), rgb(116, 168, 72)};
    const uint32_t dirt[] = {rgb(122, 88, 60), rgb(112, 80, 54), rgb(132, 96, 64)};
    const uint32_t stone[] = {rgb(128, 128, 124), rgb(116, 116, 114), rgb(140, 138, 132), rgb(104, 106, 106)};
    const uint32_t leaves[LEAF_SHADES] = {rgb(62, 64, 118), rgb(58, 92, 112), rgb(56, 112, 74), rgb(72, 136, 70),
        rgb(98, 158, 72), rgb(128, 178, 80), rgb(168, 204, 102), rgb(214, 228, 140)};
    for (int i = 0; i < 4; ++i) { grid->palette[MAT_GRASS + i] = grass[i]; grid->palette[MAT_STONE + i] = stone[i]; }
    for (int i = 0; i < 3; ++i) grid->palette[MAT_DIRT + i] = dirt[i];
    for (int i = 0; i < LEAF_SHADES; ++i) grid->palette[MAT_LEAVES + i] = leaves[i];
    grid->palette[MAT_DEEP_STONE] = rgb(96, 96, 96);
    grid->palette[MAT_SAND] = rgb(212, 196, 146); grid->palette[MAT_SAND + 1] = rgb(200, 184, 136);
    grid->palette[MAT_WATER] = rgb(52, 108, 156); grid->palette[MAT_WATER + 1] = rgb(58, 118, 164);
    grid->palette[MAT_BARK] = rgb(176, 128, 92); grid->palette[MAT_BARK + 1] = rgb(150, 104, 74);
    grid->palette[MAT_FLOWER] = rgb(222, 84, 84); grid->palette[MAT_FLOWER + 1] = rgb(240, 208, 86); grid->palette[MAT_FLOWER + 2] = rgb(238, 236, 228);
    grid->palette[MAT_BRICK] = rgb(170, 160, 146); grid->palette[MAT_BRICK + 1] = rgb(150, 142, 130);
    grid->palette[MAT_MOSS_BRICK] = rgb(112, 136, 92);
}
/** Writes terrain columns, shore sand, flowers, and the pond surface. */
static void build_terrain(AxVoxelGrid *grid)
{
    for (int z = 0; z < (int)SCENE_SIZE; ++z)
        for (int x = 0; x < (int)SCENE_SIZE; ++x) {
            int height = terrain_height(x, z);
            bool shore = height <= WATER_LEVEL + 1;
            for (int y = 0; y <= height; ++y) {
                int depth = height - y;
                uint32_t h = hash3(x, y, z);
                uint8_t material;
                if (depth == 0) material = shore ? (uint8_t)(MAT_SAND + h % 2) : (uint8_t)(MAT_GRASS + h % 4);
                else if (depth < 4) material = shore ? (uint8_t)(MAT_SAND + h % 2) : (uint8_t)(MAT_DIRT + h % 3);
                else if (depth < 10) material = (uint8_t)(MAT_STONE + h % 4);
                else material = MAT_DEEP_STONE;
                ax_voxel_grid_set(grid, x, y, z, material);
            }
            for (int y = height + 1; y <= WATER_LEVEL; ++y)
                ax_voxel_grid_set(grid, x, y, z, (uint8_t)(MAT_WATER + (y == WATER_LEVEL ? hash3(x, y, z) % 2 : 0)));
            if (!shore && random01(x, 7, z) < 0.012f)
                ax_voxel_grid_set(grid, x, height + 1, z, (uint8_t)(MAT_FLOWER + hash3(x, 9, z) % 3));
        }
}
/** Draws a one-voxel-thick bark line between two points. */
static void bark_line(AxVoxelGrid *grid, float x0, float y0, float z0, float x1, float y1, float z1)
{
    float dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    int steps = (int)ceilf(fmaxf(fabsf(dx), fmaxf(fabsf(dy), fabsf(dz)))) * 2 + 1;
    for (int i = 0; i <= steps; ++i) {
        float t = (float)i / (float)steps;
        int x = (int)floorf(x0 + dx * t), y = (int)floorf(y0 + dy * t), z = (int)floorf(z0 + dz * t);
        ax_voxel_grid_set(grid, x, y, z, (uint8_t)(MAT_BARK + hash3(x, y, z) % 2));
    }
}
/** Fills a ragged foliage clump whose shade ramps from shadowed underside to sunlit top. */
static void foliage(AxVoxelGrid *grid, float cx, float cy, float cz, float radius, float crown_bottom, float crown_top)
{
    int r = (int)ceilf(radius);
    for (int z = -r; z <= r; ++z)
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x) {
                int px = (int)cx + x, py = (int)cy + y, pz = (int)cz + z;
                float distance = sqrtf((float)(x * x + y * y + z * z));
                if (distance > radius - random01(px, py, pz) * 2.2f) continue;
                uint8_t existing = ax_voxel_grid_get(grid, px, py, pz);
                if (existing && (existing < MAT_LEAVES || existing >= MAT_LEAVES + LEAF_SHADES)) continue;
                float shade = ((float)py - crown_bottom) / fmaxf(crown_top - crown_bottom, 1.0f);
                shade += (random01(px, py + 31, pz) - 0.5f) * 0.35f;
                int index = (int)(shade * LEAF_SHADES);
                index = index < 0 ? 0 : index >= LEAF_SHADES ? LEAF_SHADES - 1 : index;
                ax_voxel_grid_set(grid, px, py, pz, (uint8_t)(MAT_LEAVES + index));
            }
}
/** Grows one stylized tree: a trunk splitting into branches with a clump at each tip. */
static void tree(AxVoxelGrid *grid, int x, int z, uint32_t seed)
{
    int ground = terrain_height(x, z);
    float trunk = 9.0f + (float)(seed % 6);
    float top = (float)ground + trunk;
    bark_line(grid, (float)x + 0.5f, (float)ground + 1.0f, (float)z + 0.5f, (float)x + 0.5f, top, (float)z + 0.5f);
    bark_line(grid, (float)x + 1.5f, (float)ground + 1.0f, (float)z + 0.5f, (float)x + 1.5f, top - 3.0f, (float)z + 0.5f);
    int branches = 3 + (int)(seed % 3);
    float crown_bottom = top - 2.0f, crown_top = top + 12.0f;
    float tips[6][3];
    for (int i = 0; i < branches; ++i) {
        float angle = (float)i / (float)branches * 6.2831853f + random01((int)seed, i, 3) * 1.2f;
        float reach = 4.0f + random01((int)seed, i, 5) * 3.0f;
        tips[i][0] = (float)x + 0.5f + cosf(angle) * reach;
        tips[i][1] = top + 3.0f + random01((int)seed, i, 7) * 4.0f;
        tips[i][2] = (float)z + 0.5f + sinf(angle) * reach;
        bark_line(grid, (float)x + 0.5f, top - 2.0f, (float)z + 0.5f, tips[i][0], tips[i][1], tips[i][2]);
    }
    for (int i = 0; i < branches; ++i)
        foliage(grid, tips[i][0], tips[i][1], tips[i][2], 4.5f + random01((int)seed, i, 11) * 2.0f, crown_bottom, crown_top);
    foliage(grid, (float)x + 0.5f, top + 6.0f, (float)z + 0.5f, 5.5f + (float)(seed % 3), crown_bottom, crown_top);
}
/** Scatters trees on dry ground with a minimum spacing. */
static void build_trees(AxVoxelGrid *grid)
{
    int placed[TREE_COUNT][2], count = 0;
    for (uint32_t attempt = 0; attempt < 4096 && count < TREE_COUNT; ++attempt) {
        int x = 16 + (int)(hash3((int)attempt, 1, 2) % (SCENE_SIZE - 32));
        int z = 16 + (int)(hash3((int)attempt, 3, 4) % (SCENE_SIZE - 32));
        if (terrain_height(x, z) <= WATER_LEVEL + 2) continue;
        if (x > 40 && x < 84 && z > 150 && z < 194) continue;
        if ((x - SPAWN_X) * (x - SPAWN_X) + (z - SPAWN_Z) * (z - SPAWN_Z) < 40 * 40) continue;
        bool crowded = false;
        for (int i = 0; i < count && !crowded; ++i) {
            int dx = placed[i][0] - x, dz = placed[i][1] - z;
            crowded = dx * dx + dz * dz < 18 * 18;
        }
        if (crowded) continue;
        tree(grid, x, z, hash3(x, 5, z));
        placed[count][0] = x; placed[count][1] = z; ++count;
    }
}
/** Builds a crumbling hollow stone tower with mossy lower courses. */
static void build_ruin(AxVoxelGrid *grid)
{
    const int cx = 62, cz = 172, radius = 8;
    int base = terrain_height(cx, cz);
    for (int z = -radius - 2; z <= radius + 2; ++z)
        for (int x = -radius - 2; x <= radius + 2; ++x) {
            int ground = terrain_height(cx + x, cz + z);
            for (int y = ground + 1; y < base; ++y) ax_voxel_grid_set(grid, cx + x, y, cz + z, MAT_BRICK);
        }
    for (int z = -radius; z <= radius; ++z)
        for (int x = -radius; x <= radius; ++x) {
            float distance = sqrtf((float)(x * x + z * z));
            if (distance > (float)radius || distance < (float)radius - 2.0f) continue;
            float angle = atan2f((float)z, (float)x);
            int height = 14 + (int)(sinf(angle * 3.0f) * 6.0f + random01(x, 0, z) * 4.0f);
            for (int y = 0; y < height; ++y) {
                if (y % 6 == 3 && ((x + z) & 7) == 0) continue;
                uint32_t h = hash3(cx + x, base + y, cz + z);
                uint8_t material = y < 4 && h % 3 == 0 ? MAT_MOSS_BRICK : (uint8_t)(MAT_BRICK + h % 2);
                ax_voxel_grid_set(grid, cx + x, base + y, cz + z, material);
            }
        }
}
bool ax_demo_scene_build(AxOctree *octree, AxCamera *camera)
{
    if (!octree) return false;
    *octree = (AxOctree){0};
    AxVoxelGrid grid;
    if (!ax_voxel_grid_create(&grid, SCENE_SIZE)) return false;
    fill_palette(&grid);
    build_terrain(&grid);
    build_ruin(&grid);
    build_trees(&grid);
    bool built = ax_octree_build(octree, &grid);
    ax_voxel_grid_destroy(&grid);
    if (built && camera) {
        const float spawn[3] = {(float)SPAWN_X, (float)terrain_height(SPAWN_X, SPAWN_Z) + 30.0f, (float)SPAWN_Z};
        ax_camera_init(camera, spawn, 0.0f, -0.32f);
    }
    return built;
}
