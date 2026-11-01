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
        float span=view.light_right.w;
        gl_Position=vec4(dot(p,view.light_right.xyz)/span,-dot(p,view.light_up.xyz)/span,
            0.5-dot(p,view.sun.xyz)/(span*4.0),1.0);
    } else {
        float z=dot(p,view.forward.xyz);
        gl_Position=vec4(dot(p,view.right.xyz)/(view.forward.w*view.right.w),
            -dot(p,view.up.xyz)/view.forward.w,view.up.w,z);
    }
}
