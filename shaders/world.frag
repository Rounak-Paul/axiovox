#version 450
#extension GL_GOOGLE_include_directive : require
#include "world_config.h"
// Sparse voxel octree ray caster after Laine & Karras, "Efficient Sparse Voxel Octrees" (NVR-2010-001), Figure 15.
// The octree occupies [1, 2]^3 so cube positions map directly onto float mantissa bits. Voxel shapes are the
// intersection of their cube with their own and all ancestor contour slabs (paper Sections 5.2-5.3, Appendix A)
// when built with AX_WORLD_CONTOURS; otherwise voxels are plain cubes.
layout(location=0) in vec2 ndc;
layout(location=0) out vec4 out_color;
layout(std430, set=0, binding=0) readonly buffer Octree { uint words[]; } octree;
layout(push_constant) uniform View {
    vec4 origin;   // xyz: eye in octree space; w: pixel footprint (radians per pixel) scaled by LOD bias
    vec4 forward;  // xyz: unit view direction; w: world units per octree unit
    vec4 right;    // xyz: right scaled by tan(fov/2) * aspect
    vec4 up;       // xyz: up scaled by tan(fov/2)
    vec4 sun;      // xyz: unit direction toward the sun; w: shadow ray origin offset in octree units
    uvec4 info;    // x: lookup offset, y: value offset, z: primary step budget, w: shadow step budget
    uvec4 extra;   // x: contour offset
} view;

const int S_MAX = 23;
const float EPSILON = 1.0 / 8388608.0;

struct Hit {
    bool found;
    float t;
    vec3 normal;
    uint parent;
    uint slot;
};

// Casts p + t * d through the octree; footprint > 0 stops at voxels smaller than the ray cone.
Hit cast_ray(vec3 p, vec3 d, float footprint, uint max_steps)
{
    Hit hit = Hit(false, 0.0, vec3(0.0), 0u, 0u);
    uint stack_parent[S_MAX + 1];
    float stack_tmax[S_MAX + 1];

    if (abs(d.x) < EPSILON) d.x = d.x < 0.0 ? -EPSILON : EPSILON;
    if (abs(d.y) < EPSILON) d.y = d.y < 0.0 ? -EPSILON : EPSILON;
    if (abs(d.z) < EPSILON) d.z = d.z < 0.0 ? -EPSILON : EPSILON;

    // Mirror the coordinate system so the ray direction is negative along every axis.
    vec3 coef = 1.0 / -abs(d);
    vec3 bias = coef * p;
    uint octant = 7u;
    if (d.x > 0.0) { octant ^= 1u; bias.x = 3.0 * coef.x - bias.x; }
    if (d.y > 0.0) { octant ^= 2u; bias.y = 3.0 * coef.y - bias.y; }
    if (d.z > 0.0) { octant ^= 4u; bias.z = 3.0 * coef.z - bias.z; }

    float t_min = max(max(2.0 * coef.x - bias.x, 2.0 * coef.y - bias.y), 2.0 * coef.z - bias.z);
    float t_max = min(min(coef.x - bias.x, coef.y - bias.y), coef.z - bias.z);
    float h = t_max;
    vec3 facing = vec3((octant & 1u) != 0u ? 1.0 : -1.0, (octant & 2u) != 0u ? 1.0 : -1.0, (octant & 4u) != 0u ? 1.0 : -1.0);
    vec3 entry = 2.0 * coef - bias;
    vec3 normal = entry.x >= entry.y && entry.x >= entry.z ? vec3(facing.x, 0.0, 0.0)
                : entry.y >= entry.z ? vec3(0.0, facing.y, 0.0) : vec3(0.0, 0.0, facing.z);
    if (t_min <= 0.0) normal = -normalize(d);
    t_min = max(t_min, 0.0);

    uint parent = 0u;
    uint descriptor = 0u;
    uint contour_word = 0u;
    uint idx = 0u;
    vec3 pos = vec3(1.0);
    int scale = S_MAX - 1;
    float scale_exp2 = 0.5;
    if (1.5 * coef.x - bias.x > t_min) { idx ^= 1u; pos.x = 1.5; }
    if (1.5 * coef.y - bias.y > t_min) { idx ^= 2u; pos.y = 1.5; }
    if (1.5 * coef.z - bias.z > t_min) { idx ^= 4u; pos.z = 1.5; }

    uint steps = 0u;
    while (scale < S_MAX) {
        if (++steps > max_steps) return hit;
        if (descriptor == 0u) {
            descriptor = octree.words[parent * 2u];
#if AX_WORLD_CONTOURS
            contour_word = octree.words[parent * 2u + 1u];
#endif
        }

        vec3 corner = pos * coef - bias;
        float tc_max = min(min(corner.x, corner.y), corner.z);

        uint child_masks = descriptor << (idx ^ octant);
        if ((child_masks & 0x8000u) != 0u && t_min <= t_max) {
            if (tc_max * footprint >= scale_exp2) break;

            // INTERSECT
            float tv_max = min(t_max, tc_max);
            float half_size = scale_exp2 * 0.5;
            vec3 center = half_size * coef + corner;

#if AX_WORLD_CONTOURS
            // Intersect the active span with the voxel's contour slab.
            uint contour_mask = contour_word << (idx ^ octant);
            if ((contour_mask & 0x80u) != 0u) {
                uint value = octree.words[view.extra.x + (contour_word >> 8) + uint(bitCount(contour_mask & 0x7Fu))];
                vec3 cn = vec3(bitfieldExtract(int(value), 12, 6), bitfieldExtract(int(value), 6, 6), bitfieldExtract(int(value), 0, 6)) / 32.0;
                float cpos = float(bitfieldExtract(int(value), 18, 7)) * scale_exp2 * (1.5 / 64.0);
                float cthick = float(value >> 25) * scale_exp2 * (0.75 / 64.0);
                vec3 cdir = cn * d;
                float denom = cdir.x + cdir.y + cdir.z;
                if (abs(denom) < 1e-20) denom = denom < 0.0 ? -1e-20 : 1e-20;
                float tcoef = 1.0 / denom;
                float tavg = dot(center, cdir) + cpos;
                float tdiff = abs(cthick * tcoef);
                float slab_min = tcoef * tavg - tdiff;
                if (slab_min > t_min) {
                    t_min = slab_min;
                    normal = normalize(denom < 0.0 ? cn : -cn);
                }
                tv_max = min(tv_max, tcoef * tavg + tdiff);
            }
#endif

            if (t_min <= tv_max) {
                if ((child_masks & 0x0080u) == 0u) break;

                // PUSH
                if (tc_max < h) {
                    stack_parent[scale] = parent;
                    stack_tmax[scale] = t_max;
                }
                h = tc_max;

                uint offset = descriptor >> 17;
                if ((descriptor & 0x10000u) != 0u) offset = octree.words[(parent + offset) * 2u];
                offset += uint(bitCount(child_masks & 0x7Fu));
                parent += offset;

                idx = 0u;
                scale--;
                scale_exp2 = half_size;
                if (center.x > t_min) { idx ^= 1u; pos.x += scale_exp2; }
                if (center.y > t_min) { idx ^= 2u; pos.y += scale_exp2; }
                if (center.z > t_min) { idx ^= 4u; pos.z += scale_exp2; }

                t_max = tv_max;
                descriptor = 0u;
                continue;
            }
        }

        // ADVANCE
        uint step_mask = 0u;
        if (corner.x <= tc_max) { step_mask ^= 1u; pos.x -= scale_exp2; }
        if (corner.y <= tc_max) { step_mask ^= 2u; pos.y -= scale_exp2; }
        if (corner.z <= tc_max) { step_mask ^= 4u; pos.z -= scale_exp2; }
        if (tc_max > t_min) normal = (step_mask & 1u) != 0u ? vec3(facing.x, 0.0, 0.0)
                                   : (step_mask & 2u) != 0u ? vec3(0.0, facing.y, 0.0) : vec3(0.0, 0.0, facing.z);
        t_min = tc_max;
        idx ^= step_mask;

        if ((idx & step_mask) != 0u) {
            // POP: the highest differing position bit names the common ancestor's scale.
            uint differing = 0u;
            if ((step_mask & 1u) != 0u) differing |= floatBitsToUint(pos.x) ^ floatBitsToUint(pos.x + scale_exp2);
            if ((step_mask & 2u) != 0u) differing |= floatBitsToUint(pos.y) ^ floatBitsToUint(pos.y + scale_exp2);
            if ((step_mask & 4u) != 0u) differing |= floatBitsToUint(pos.z) ^ floatBitsToUint(pos.z + scale_exp2);
            scale = findMSB(differing);
            if (scale >= S_MAX) break;
            scale_exp2 = uintBitsToFloat(uint(scale - S_MAX + 127) << 23);

            parent = stack_parent[scale];
            t_max = stack_tmax[scale];

            uvec3 shifted = floatBitsToUint(pos) >> uint(scale);
            pos = uintBitsToFloat(shifted << uint(scale));
            idx = (shifted.x & 1u) | ((shifted.y & 1u) << 1) | ((shifted.z & 1u) << 2);

            h = 0.0;
            descriptor = 0u;
        }
    }
    if (scale >= S_MAX) return hit;

    hit.found = true;
    hit.normal = normal;
    hit.t = t_min;
    hit.parent = parent;
    hit.slot = idx ^ octant ^ 7u;
    return hit;
}

// Decodes the ABGR attachment value stored for a child slot of a descriptor.
vec3 voxel_albedo(uint parent, uint slot)
{
    uint lookup = octree.words[view.info.x + parent];
    uint bit = 1u << slot;
    if ((lookup & bit) == 0u) return vec3(1.0, 0.0, 1.0);
    uint value = octree.words[view.info.y + (lookup >> 8) + uint(bitCount(lookup & 0xFFu & (bit - 1u)))];
    return vec3(uvec3(value, value >> 8, value >> 16) & 0xFFu) / 255.0;
}

vec3 sky(vec3 d)
{
    vec3 horizon = vec3(0.78, 0.84, 0.88);
    vec3 zenith = vec3(0.32, 0.52, 0.78);
    vec3 color = mix(horizon, zenith, clamp(d.y * 1.4, 0.0, 1.0));
    float sun = max(dot(d, view.sun.xyz), 0.0);
    return color + vec3(1.0, 0.9, 0.7) * (pow(sun, 900.0) * 4.0 + pow(sun, 12.0) * 0.18);
}

void main() {
    vec3 d = normalize(view.forward.xyz + view.right.xyz * ndc.x - view.up.xyz * ndc.y);
    vec3 p = view.origin.xyz;
    Hit hit = cast_ray(p, d, view.origin.w, view.info.z);
    vec3 background = sky(d);
    vec3 color = background;
    if (hit.found) {
        vec3 albedo = pow(voxel_albedo(hit.parent, hit.slot), vec3(2.2));
        vec3 point = p + d * hit.t;
        vec3 n = hit.normal;
        float diffuse = max(dot(n, view.sun.xyz), 0.0);
        if (diffuse > 0.0) {
            Hit shadow = cast_ray(point + n * view.sun.w, view.sun.xyz, 0.0, view.info.w);
            if (shadow.found) diffuse = 0.0;
        }
        vec3 ambient = mix(vec3(0.22, 0.2, 0.24), vec3(0.42, 0.5, 0.62), n.y * 0.5 + 0.5);
        color = albedo * (ambient + vec3(1.0, 0.94, 0.84) * diffuse * 1.15);
        float distance = hit.t * view.forward.w;
        color = mix(color, pow(background, vec3(2.2)), 1.0 - exp(-distance * 0.012));
        color = pow(color, vec3(1.0 / 2.2));
    }
    out_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
