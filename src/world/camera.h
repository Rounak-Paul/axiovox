#pragma once
#include "core/input.h"
/**
 * Free-flight perspective camera in right-handed, Y-up world units.
 * Yaw 0 looks toward -Z; positive yaw turns toward +X; positive pitch looks up.
 */
typedef struct {
    float position[3];
    float yaw, pitch;
    float fov_y;
    float move_speed;
    float boost;
    float sensitivity;
} AxCamera;
/**
 * Initializes a camera with default lens and controller tuning.
 * @param position World position; NULL places the camera at the origin.
 * @param yaw,pitch Orientation in radians; pitch is clamped short of the poles and non-finite values reset to zero.
 */
void ax_camera_init(AxCamera *camera, const float position[3], float yaw, float pitch);
/**
 * Computes the orthonormal view basis.
 * @param forward,right,up Receive unit vectors; any may be NULL.
 */
void ax_camera_basis(const AxCamera *camera, float forward[3], float right[3], float up[3]);
/**
 * Applies free-flight controls: mouse look, W/S along the view direction, A/D strafe, E/Q along world up, Shift boost.
 * Honors UI input capture through the gameplay input accessors.
 * @param dt Frame time in seconds; non-finite or non-positive values are ignored and long stalls are clamped.
 */
void ax_camera_update(AxCamera *camera, const AxInput *input, double dt);
