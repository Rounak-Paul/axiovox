#include "world/camera.h"
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
    ax_camera_init(&camera, NULL, 0.0f, 10.0f,NULL);
    CHECK(camera.pitch < 1.5708f && camera.pitch > 1.56f);
    ax_camera_init(&camera, (float[3]){0, 0, 0}, NAN, NAN,NULL);
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
/** Runs GPU-independent camera and input regressions. */
int main(void) { return camera_tests() ? EXIT_SUCCESS : EXIT_FAILURE; }
