#pragma once
#include "core/input.h"
/**
 * Free-flight perspective camera in a transported, gravity-aligned tangent frame.
 * Yaw/pitch operate in the transported tangent frame; default orientation looks toward -Z.
 */
typedef struct {
    float position[3];
    float reference_up[3],reference_forward[3];
    float yaw, pitch;
    float fov_y;
    float move_speed;
    float boost;
    float sensitivity;
} AxCamera;
/**
 * Initializes a camera with default lens and controller tuning.
 * @param position World position; NULL places the camera at the origin.
 * @param up Initial gravity-up direction; NULL uses world Y.
 * @param yaw,pitch Orientation in radians; pitch is clamped short of the poles and non-finite values reset to zero.
 */
void ax_camera_init(AxCamera *camera, const float position[3], float yaw, float pitch,const double up[3]);
/**
 * Computes the orthonormal view basis.
 * @param forward,right,up Receive unit vectors; any may be NULL.
 */
void ax_camera_basis(const AxCamera *camera, float forward[3], float right[3], float up[3]);
/**
 * Applies free-flight controls: mouse look, W/S along the view direction, A/D strafe, E/Q along local up, Shift boost.
 * Honors UI input capture through the gameplay input accessors.
 * @param dt Frame time in seconds; non-finite or non-positive values are ignored and long stalls are clamped.
 */
void ax_camera_update(AxCamera *camera, const AxInput *input, double dt);

/** Smoothly transports the flight frame toward up by influence, using dt seconds; zero influence preserves space orientation. */
void ax_camera_align(AxCamera *camera,const double up[3],double influence,double dt);
