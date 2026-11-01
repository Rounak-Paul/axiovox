#include "world/demo_scene.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    CHECK(near(camera.position[2], -0.3f) && near(camera.position[0], 0.0f));
    ax_camera_update(&camera, &input, 10.0);
    CHECK(near(camera.position[2], -0.9f));
    ax_camera_update(&camera, &input, NAN);
    ax_camera_update(&camera, &input, -1.0);
    CHECK(near(camera.position[2], -0.9f));
    ax_input_key(&input, AX_KEY_W, false, false, 0);

    ax_input_key(&input, AX_KEY_E, true, false, 0);
    ax_input_key(&input, AX_KEY_LEFT_SHIFT, true, false, 0);
    ax_camera_update(&camera, &input, 0.1);
    CHECK(near(camera.position[1], 2.4f));
    ax_input_key(&input, AX_KEY_LEFT_SHIFT, false, false, 0);
    ax_input_key(&input, AX_KEY_D, true, false, 0);
    float before[3] = {camera.position[0], camera.position[1], camera.position[2]};
    ax_camera_update(&camera, &input, 0.1);
    float moved = sqrtf(powf(camera.position[0] - before[0], 2) + powf(camera.position[1] - before[1], 2));
    CHECK(near(moved, 0.6f));
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
typedef struct { float center[3], radius; } Sphere;
/** Exact sphere distance. */
static float sphere_distance(const void *user, const float p[3])
{
    const Sphere *s = user;
    float d[3] = {p[0] - s->center[0], p[1] - s->center[1], p[2] - s->center[2]};
    return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - s->radius;
}
/** Gyroid surface scaled to stay 1-Lipschitz; fills space with surface to force far pointers. */
static float gyroid_distance(const void *user, const float p[3])
{
    (void)user;
    const float k = 0.9f;
    return (sinf(p[0] * k) * cosf(p[1] * k) + sinf(p[1] * k) * cosf(p[2] * k) + sinf(p[2] * k) * cosf(p[0] * k)) / (k * 2.5f);
}
/** Distance with no surface inside the volume. */
static float far_distance(const void *user, const float p[3]) { (void)user; (void)p; return 1000.0f; }
/** Constant test color. */
static uint32_t test_color(const void *user, const float p[3], const float n[3], const int32_t cell[3])
{
    (void)user; (void)p; (void)n; (void)cell;
    return 0xff3366ccu;
}
/** Deterministic pseudo-random float in [0, 1). */
static float next_random(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return (float)(*state >> 8) / 16777216.0f;
}
/** Accepts a surface point when the octree contains it within a tiny tolerance along the normal. */
static bool encloses(const AxOctree *octree, const float p[3], const float n[3], float voxel)
{
    for (int step = -1; step <= 1; ++step) {
        float q[3] = {p[0] + n[0] * voxel * 0.01f * (float)step, p[1] + n[1] * voxel * 0.01f * (float)step, p[2] + n[2] * voxel * 0.01f * (float)step};
        if (ax_octree_sample(octree, q, NULL)) return true;
    }
    return false;
}
/** Verifies a sphere is enclosed without holes, excludes off-surface points, and builds identically in parallel. */
static bool sphere_tests(AxJobs *jobs)
{
    Sphere sphere = {{8.0f, 8.3f, 7.7f}, 5.1f};
    AxVoxelSource source = {sphere_distance, test_color, &sphere, 16.0f, 6, 1.0f};
    AxOctree serial, parallel;
    CHECK(ax_octree_build(&serial, &source, NULL));
    CHECK(ax_octree_build(&parallel, &source, jobs));
    CHECK(serial.word_count == parallel.word_count && !memcmp(serial.words, parallel.words, serial.word_count * sizeof(uint32_t)));
    CHECK((AX_WORLD_CONTOURS ? serial.contour_count > serial.value_count / 2 : serial.contour_count == 0) && serial.depth == 6 && serial.extent == 16.0f);
    float voxel = 16.0f / 64.0f;
    uint32_t state = 7u, misses = 0;
    for (int i = 0; i < 20000; ++i) {
        float u[3] = {next_random(&state) * 2 - 1, next_random(&state) * 2 - 1, next_random(&state) * 2 - 1};
        float length = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
        if (length < 0.1f || length > 1.0f) continue;
        for (int a = 0; a < 3; ++a) u[a] /= length;
        float p[3], outside[3], inside[3];
        for (int a = 0; a < 3; ++a) {
            p[a] = sphere.center[a] + u[a] * sphere.radius;
            outside[a] = p[a] + u[a] * voxel * 2.0f;
            inside[a] = p[a] - u[a] * voxel * 2.0f;
        }
        uint32_t color = 0;
        if (!encloses(&serial, p, u, voxel)) ++misses;
        else CHECK(ax_octree_sample(&serial, p, &color) ? color == 0xff3366ccu : true);
        CHECK(!ax_octree_sample(&serial, outside, NULL) && !ax_octree_sample(&serial, inside, NULL));
    }
    if (misses) fprintf(stderr, "sphere enclosure misses: %u\n", misses);
    CHECK(misses == 0);
    CHECK(!ax_octree_sample(&serial, (float[3]){-1, 8, 8}, NULL) && !ax_octree_sample(&serial, (float[3]){8, 16, 8}, NULL));
    CHECK(!ax_octree_sample(&serial, (float[3]){NAN, 8, 8}, NULL));
    ax_octree_destroy(&serial);
    ax_octree_destroy(&parallel);
    return true;
}
/** Verifies invalid sources, empty volumes, and a dense surface that requires far pointers. */
static bool octree_tests(AxJobs *jobs)
{
    AxOctree octree;
    AxVoxelSource source = {far_distance, test_color, NULL, 16.0f, 4, 1.0f};
    CHECK(!ax_octree_build(&octree, NULL, NULL) && !octree.words);
    AxVoxelSource bad = source; bad.depth = 0;
    CHECK(!ax_octree_build(&octree, &bad, NULL));
    bad.depth = AX_OCTREE_MAX_DEPTH + 1;
    CHECK(!ax_octree_build(&octree, &bad, NULL));
    bad = source; bad.extent = 0.0f;
    CHECK(!ax_octree_build(&octree, &bad, NULL));
    bad = source; bad.lipschitz = NAN;
    CHECK(!ax_octree_build(&octree, &bad, NULL));
    bad = source; bad.color = NULL;
    CHECK(!ax_octree_build(&octree, &bad, NULL));
    ax_octree_destroy(NULL);
    CHECK(ax_octree_build(&octree, &source, jobs));
    CHECK(octree.descriptor_count == 1 && !ax_octree_sample(&octree, (float[3]){3, 3, 3}, NULL));
    ax_octree_destroy(&octree);

    AxVoxelSource gyroid = {gyroid_distance, test_color, NULL, 64.0f, 7, 1.0f};
    CHECK(ax_octree_build(&octree, &gyroid, jobs));
    CHECK(octree.far_count > 0 && (AX_WORLD_CONTOURS ? octree.contour_count > 0 : octree.contour_count == 0));
    float voxel = 64.0f / 128.0f;
    uint32_t state = 11u, misses = 0, tested = 0;
    for (int i = 0; i < 20000; ++i) {
        float a[3] = {next_random(&state) * 63.0f + 0.5f, next_random(&state) * 63.0f + 0.5f, next_random(&state) * 63.0f + 0.5f};
        float b[3] = {a[0] + next_random(&state) * 1.6f - 0.8f, a[1] + next_random(&state) * 1.6f - 0.8f, a[2] + next_random(&state) * 1.6f - 0.8f};
        float fa = gyroid_distance(NULL, a), fb = gyroid_distance(NULL, b);
        if ((fa < 0) == (fb < 0) || b[0] <= 0 || b[1] <= 0 || b[2] <= 0 || b[0] >= 64 || b[1] >= 64 || b[2] >= 64) continue;
        for (int iteration = 0; iteration < 40; ++iteration) {
            float m[3] = {(a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f, (a[2] + b[2]) * 0.5f};
            if ((gyroid_distance(NULL, m) < 0) == (fa < 0)) { a[0] = m[0]; a[1] = m[1]; a[2] = m[2]; }
            else { b[0] = m[0]; b[1] = m[1]; b[2] = m[2]; }
        }
        float n[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (length > 0) for (int k = 0; k < 3; ++k) n[k] /= length;
        ++tested;
        if (!encloses(&octree, a, n, voxel)) ++misses;
    }
    if (misses) fprintf(stderr, "gyroid enclosure misses: %u of %u\n", misses, tested);
    CHECK(tested > 1000 && misses == 0);
    ax_octree_destroy(&octree);
    return true;
}
/** Builds the temporary demo world and checks the spawn is clear with ground below it. */
static bool scene_tests(AxJobs *jobs)
{
    AxOctree octree;
    AxCamera camera;
    CHECK(!ax_demo_scene_build(NULL, &camera, jobs));
    CHECK(ax_demo_scene_build(&octree, &camera, jobs));
    CHECK(octree.size == 1024 && octree.extent == 64.0f && octree.descriptor_count > 1 && (AX_WORLD_CONTOURS ? octree.contour_count > 0 : octree.contour_count == 0));
    CHECK(!ax_octree_sample(&octree, camera.position, NULL));
    bool ground = false;
    for (float y = camera.position[1]; y > 0.0f && !ground; y -= 0.01f)
        ground = ax_octree_sample(&octree, (float[3]){camera.position[0], y, camera.position[2]}, NULL);
    CHECK(ground);
    ax_octree_destroy(&octree);
    return true;
}
/** Runs GPU-independent camera, input, octree enclosure, parallel determinism, and scene tests. */
int main(void)
{
    AxJobs *jobs = ax_jobs_create(4);
    if (!jobs) return EXIT_FAILURE;
    bool passed = camera_tests() && octree_tests(jobs) && sphere_tests(jobs) && scene_tests(jobs);
    ax_jobs_destroy(jobs);
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
