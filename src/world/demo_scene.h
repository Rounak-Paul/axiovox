#pragma once
#include "camera.h"
#include "octree.h"
/**
 * Temporary hardcoded world used until a scene hierarchy/ECS exists: a 64 m signed-distance landscape
 * (terrain, pond, trees, ruin) voxelized at 1024^3 with contours, plus a spawn pose.
 * @param octree Receives the built world; destroy with ax_octree_destroy.
 * @param camera Receives the spawn pose; may be NULL.
 * @param jobs Optional worker pool for parallel construction (owner thread only).
 * @return false when construction fails; the octree is left empty.
 */
bool ax_demo_scene_build(AxOctree *octree, AxCamera *camera, AxJobs *jobs);
