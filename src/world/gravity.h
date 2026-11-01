#pragma once
#include "scene.h"
/** Physical acceleration plus the flight controller's smooth alignment influence. */
typedef struct {
    double acceleration[3],up[3],strength,alignment;
    AxPlanetId dominant;
} AxGravity;
/** Samples all bodies in frame-local coordinates: inverse-square outside, finite uniform-density gravity inside.
 * Zero fields/cancellation return alignment zero; output remains unchanged on invalid coordinates.
 */
bool ax_gravity_sample(const AxScene *scene,AxPlanetId frame,const AxPosition *position,AxGravity *gravity);
