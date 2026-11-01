#include "world/demo_scene.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); return false; } } while (0)
/** Compares floats within a tolerance. */
static bool near(float a, float b) { return fabsf(a - b) < 1e-4f; }
/** Verifies basis orientation, look clamping, movement axes, boost, and UI capture. */
static bool camera_tests(void)
{
    AxEvents events = {0};
    AxInput input = {.events = &events};
    AxCamera camera;
    ax_camera_init(&camera, NULL, 0.0f, 10.0f);
    CHECK(camera.pitch < 1.5708f && camera.pitch > 1.56f);
    ax_camera_init(&camera, (float[3]){0, 0, 0}, NAN, NAN);
    CHECK(camera.yaw == 0.0f && camera.pitch == 0.0f);
    float forward[3], right[3], up[3];
    ax_camera_basis(&camera, forward, right, up);
    CHECK(near(forward[2], -1.0f) && near(right[0], 1.0f) && near(up[1], 1.0f));

    ax_input_key(&input, AX_KEY_W, true, false, 0);
    ax_camera_update(&camera, &input, 0.05);
    CHECK(near(camera.position[2], -1.2f) && near(camera.position[0], 0.0f));
    ax_camera_update(&camera, &input, 10.0);
    CHECK(near(camera.position[2], -3.6f));
    ax_camera_update(&camera, &input, NAN);
    ax_camera_update(&camera, &input, -1.0);
    CHECK(near(camera.position[2], -3.6f));
    ax_input_key(&input, AX_KEY_W, false, false, 0);

    ax_input_key(&input, AX_KEY_E, true, false, 0);
    ax_input_key(&input, AX_KEY_LEFT_SHIFT, true, false, 0);
    ax_camera_update(&camera, &input, 0.1);
    CHECK(near(camera.position[1], 9.6f));
    ax_input_key(&input, AX_KEY_LEFT_SHIFT, false, false, 0);
    ax_input_key(&input, AX_KEY_D, true, false, 0);
    float before[3] = {camera.position[0], camera.position[1], camera.position[2]};
    ax_camera_update(&camera, &input, 0.1);
    float moved = sqrtf(powf(camera.position[0] - before[0], 2) + powf(camera.position[1] - before[1], 2));
    CHECK(near(moved, 2.4f));
    ax_input_key(&input, AX_KEY_D, false, false, 0);
    ax_input_key(&input, AX_KEY_E, false, false, 0);

    ax_input_begin_frame(&input);
    ax_input_mouse_move(&input, 100, 100);
    ax_input_mouse_move(&input, 140, 80);
    ax_camera_update(&camera, &input, 0.016);
    CHECK(camera.yaw == 0.0f);
    ax_input_set_pointer_locked(&input, true);
    ax_input_mouse_move(&input, 500, 500);
    ax_input_mouse_move(&input, 540, 480);
    ax_camera_update(&camera, &input, 0.016);
    CHECK(near(camera.yaw, 0.1f) && near(camera.pitch, 0.05f));
    ax_input_begin_frame(&input);
    input.captured = true;
    ax_input_mouse_move(&input, 900, 900);
    ax_input_key(&input, AX_KEY_W, true, false, 0);
    before[0] = camera.position[0]; before[1] = camera.position[1]; before[2] = camera.position[2];
    ax_camera_update(&camera, &input, 0.1);
    CHECK(near(camera.yaw, 0.1f) && camera.position[0] == before[0] && camera.position[2] == before[2]);
    input.captured = false;
    ax_input_focus(&input, false);
    ax_input_begin_frame(&input);
    ax_input_mouse_move(&input, 0, 0);
    double dx, dy;
    CHECK(ax_input_mouse_delta(&input, &dx, &dy) && dx == 0 && dy == 0);
    ax_input_mouse_move(&input, 1e9, -1e9);
    for (int i = 0; i < 1000; ++i) ax_input_mouse_move(&input, i % 2 ? 1e9 : -1e9, 0);
    ax_camera_update(&camera, &input, 0.016);
    CHECK(isfinite(camera.yaw) && camera.yaw >= -3.1416f && camera.yaw <= 3.1416f);
    CHECK(camera.pitch >= -1.5708f && camera.pitch <= 1.5708f);
    return true;
}
/** Checks every grid cell against the encoded octree's point query. */
static bool matches(const AxOctree *octree, const AxVoxelGrid *grid)
{
    for (uint32_t z = 0; z < grid->size; ++z)
        for (uint32_t y = 0; y < grid->size; ++y)
            for (uint32_t x = 0; x < grid->size; ++x) {
                uint8_t material = ax_voxel_grid_get(grid, x, y, z);
                uint32_t color = 0;
                bool solid = ax_octree_sample(octree, x, y, z, &color);
                if (solid != (material != 0) || (solid && color != (grid->palette[material] | 0xff000000u))) {
                    fprintf(stderr, "Mismatch at %u %u %u\n", x, y, z);
                    return false;
                }
            }
    return true;
}
/** Verifies empty, uniform, sparse, and far-pointer layouts round-trip exactly. */
static bool octree_tests(void)
{
    AxVoxelGrid grid;
    AxOctree octree;
    CHECK(!ax_voxel_grid_create(&grid, 0) && !ax_voxel_grid_create(&grid, 1) && !ax_voxel_grid_create(&grid, 12));
    CHECK(!ax_voxel_grid_create(&grid, 1u << 17));
    CHECK(!ax_octree_build(&octree, NULL) && !octree.words);
    ax_octree_destroy(&octree);
    ax_voxel_grid_destroy(NULL);

    CHECK(ax_voxel_grid_create(&grid, 8));
    for (int i = 1; i < 256; ++i) grid.palette[i] = 0xff000000u | (uint32_t)i * 0x010203u;
    CHECK(ax_octree_build(&octree, &grid));
    CHECK(octree.descriptor_count == 1 && !ax_octree_sample(&octree, 3, 3, 3, NULL));
    ax_octree_destroy(&octree);
    for (int z = 0; z < 8; ++z) for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) ax_voxel_grid_set(&grid, x, y, z, 4);
    CHECK(ax_octree_build(&octree, &grid));
    CHECK(octree.descriptor_count == 1 && octree.value_count == 8 && matches(&octree, &grid));
    ax_octree_destroy(&octree);
    ax_voxel_grid_set(&grid, 5, 6, 7, 9);
    ax_voxel_grid_set(&grid, 0, 0, 0, 0);
    ax_voxel_grid_set(&grid, -1, 0, 99, 9);
    CHECK(ax_voxel_grid_get(&grid, -1, 0, 0) == 0 && ax_voxel_grid_get(&grid, 8, 0, 0) == 0);
    CHECK(ax_octree_build(&octree, &grid));
    CHECK(matches(&octree, &grid) && !ax_octree_sample(&octree, -1, 0, 0, NULL) && !ax_octree_sample(&octree, 0, 8, 0, NULL));
    ax_octree_destroy(&octree);
    ax_voxel_grid_destroy(&grid);

    CHECK(ax_voxel_grid_create(&grid, 128));
    for (int i = 1; i < 256; ++i) grid.palette[i] = 0xff000000u | (uint32_t)i * 0x00030507u;
    uint32_t state = 0x12345678u;
    for (uint32_t z = 0; z < 128; ++z) for (uint32_t y = 0; y < 128; ++y) for (uint32_t x = 0; x < 128; ++x) {
        state = state * 1664525u + 1013904223u;
        if ((state >> 24) < 90) ax_voxel_grid_set(&grid, x, y, z, (uint8_t)(1 + (state >> 8) % 200));
    }
    CHECK(ax_octree_build(&octree, &grid));
    CHECK(octree.far_count > 0 && octree.depth == 7);
    CHECK(matches(&octree, &grid));
    ax_octree_destroy(&octree);
    ax_voxel_grid_destroy(&grid);
    return true;
}
/** Builds the temporary demo world and checks the spawn sits above the surface inside the octree. */
static bool scene_tests(void)
{
    AxOctree octree;
    AxCamera camera;
    CHECK(!ax_demo_scene_build(NULL, &camera));
    CHECK(ax_demo_scene_build(&octree, &camera));
    CHECK(octree.size == 256 && octree.descriptor_count > 1);
    CHECK(camera.position[1] > 0 && camera.position[1] < 256);
    CHECK(!ax_octree_sample(&octree, (int64_t)camera.position[0], (int64_t)camera.position[1], (int64_t)camera.position[2], NULL));
    CHECK(ax_octree_sample(&octree, 128, 0, 128, NULL));
    ax_octree_destroy(&octree);
    return true;
}
/** Runs GPU-independent camera, input, octree, and scene tests. */
int main(void)
{
    return camera_tests() && octree_tests() && scene_tests() ? EXIT_SUCCESS : EXIT_FAILURE;
}
