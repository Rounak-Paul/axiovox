#pragma once
#include "camera.h"
#include "octree.h"
/**
 * Temporary hardcoded world used until a scene hierarchy/ECS exists.
 * Generates terrain, water, trees and a ruin into a voxel grid, builds the octree, and places the camera.
 * @param octree Receives the built world; destroy with ax_octree_destroy.
 * @param camera Receives the spawn pose; may be NULL.
 * @return false when generation or octree construction fails; outputs are left empty.
 */
bool ax_demo_scene_build(AxOctree *octree, AxCamera *camera);
