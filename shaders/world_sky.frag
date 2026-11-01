#version 450
#extension GL_GOOGLE_include_directive : require
#include "world_common.glsl"
layout(location=0) in vec2 ndc;
layout(location=0) out vec4 out_color;
void main()
{
    vec3 d=normalize(view.forward.xyz+view.right.xyz*ndc.x*view.forward.w*view.right.w-view.up.xyz*ndc.y*view.forward.w);
    out_color=vec4(clamp(sky_color(d),0.0,1.0),1.0);
}
