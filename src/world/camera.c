#include "camera.h"
#include <math.h>
#include <stddef.h>
#define AX_PI 3.14159265358979323846f
#define AX_PITCH_LIMIT (AX_PI * 0.5f - 0.001f)
#define AX_MAX_FRAME_TIME 0.1
/** Clamps pitch short of the poles so the basis never degenerates. */
static float clamp_pitch(float pitch)
{
    if (!isfinite(pitch)) return 0.0f;
    return pitch > AX_PITCH_LIMIT ? AX_PITCH_LIMIT : pitch < -AX_PITCH_LIMIT ? -AX_PITCH_LIMIT : pitch;
}
/** Wraps yaw into [-pi, pi] to preserve float precision over long sessions. */
static float wrap_yaw(float yaw)
{
    if (!isfinite(yaw)) return 0.0f;
    return remainderf(yaw, 2.0f * AX_PI);
}
void ax_camera_init(AxCamera *camera, const float position[3], float yaw, float pitch)
{
    if (!camera) return;
    *camera = (AxCamera){.yaw = wrap_yaw(yaw), .pitch = clamp_pitch(pitch), .fov_y = 70.0f * AX_PI / 180.0f,
        .move_speed = 6.0f, .boost = 4.0f, .sensitivity = 0.0025f};
    if (position)
        for (int i = 0; i < 3; ++i) camera->position[i] = isfinite(position[i]) ? position[i] : 0.0f;
}
void ax_camera_basis(const AxCamera *camera, float forward[3], float right[3], float up[3])
{
    float cy = cosf(camera->yaw), sy = sinf(camera->yaw), cp = cosf(camera->pitch), sp = sinf(camera->pitch);
    float f[3] = {sy * cp, sp, -cy * cp}, r[3] = {cy, 0.0f, sy};
    float u[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    for (int i = 0; i < 3; ++i) {
        if (forward) forward[i] = f[i];
        if (right) right[i] = r[i];
        if (up) up[i] = u[i];
    }
}
void ax_camera_update(AxCamera *camera, const AxInput *input, double dt)
{
    if (!camera || !input || !isfinite(dt) || dt <= 0) return;
    if (dt > AX_MAX_FRAME_TIME) dt = AX_MAX_FRAME_TIME;
    double dx, dy;
    if (ax_input_mouse_delta(input, &dx, &dy)) {
        camera->yaw = wrap_yaw(camera->yaw + (float)dx * camera->sensitivity);
        camera->pitch = clamp_pitch(camera->pitch - (float)dy * camera->sensitivity);
    }
    float forward[3], right[3];
    ax_camera_basis(camera, forward, right, NULL);
    float axial = (float)ax_input_down(input, AX_KEY_W) - (float)ax_input_down(input, AX_KEY_S);
    float lateral = (float)ax_input_down(input, AX_KEY_D) - (float)ax_input_down(input, AX_KEY_A);
    float vertical = (float)ax_input_down(input, AX_KEY_E) - (float)ax_input_down(input, AX_KEY_Q);
    float move[3] = {forward[0] * axial + right[0] * lateral, forward[1] * axial + vertical, forward[2] * axial + right[2] * lateral};
    float length = sqrtf(move[0] * move[0] + move[1] * move[1] + move[2] * move[2]);
    if (length < 1e-6f) return;
    bool boosted = ax_input_down(input, AX_KEY_LEFT_SHIFT) || ax_input_down(input, AX_KEY_RIGHT_SHIFT);
    float distance = camera->move_speed * (boosted ? camera->boost : 1.0f) * (float)dt / length;
    for (int i = 0; i < 3; ++i) camera->position[i] += move[i] * distance;
}
