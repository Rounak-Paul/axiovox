#version 450
#extension GL_GOOGLE_include_directive : require
#include "world_common.glsl"
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in uint color;
layout(location=0) out vec3 relative_position;
layout(location=1) out vec3 surface_normal;
layout(location=2) out vec3 albedo;
void main()
{
    vec3 p=position+regions.items[view.info.x].offset_extent.xyz;
    relative_position=p;
    surface_normal=normal;
    albedo=vec3(uvec3(color,color>>8,color>>16)&255u)/255.0;
    if (view.info.z!=0u) {
        vec2 plane=light_plane(p);
        gl_Position=vec4(plane.x,-plane.y,light_depth(p),1.0);
    } else {
        float z=dot(p,view.forward.xyz);
        gl_Position=vec4(dot(p,view.right.xyz)/(view.forward.w*view.right.w),
            -dot(p,view.up.xyz)/view.forward.w,view.up.w,z);
    }
}
