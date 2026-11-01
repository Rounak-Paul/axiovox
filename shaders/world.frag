#version 450
#extension GL_GOOGLE_include_directive : require
#include "world_common.glsl"
#ifndef AX_WORLD_SHADOW
#define AX_WORLD_SHADOW 0
#endif
layout(location=0) in vec3 relative_position;
layout(location=1) in vec3 surface_normal;
layout(location=2) in vec3 albedo;
#if !AX_WORLD_SHADOW
layout(set=0,binding=1) uniform sampler2DShadow shadow_map;
layout(location=0) out vec4 out_color;
float visibility(vec3 p,vec3 n)
{
    float span=view.light_right.w;
    vec3 uv=vec3(dot(p,view.light_right.xyz)/span*0.5+0.5,
        -dot(p,view.light_up.xyz)/span*0.5+0.5,0.5-dot(p,view.sun.xyz)/(span*4.0));
    if (any(lessThan(uv,vec3(0.001))) || any(greaterThan(uv,vec3(0.999)))) return 1.0;
    float bias=max(0.0001,0.0004*(1.0-dot(n,view.sun.xyz)));
    float sum=0.0;
    vec2 texel=1.0/vec2(textureSize(shadow_map,0));
    for (int y=0;y<2;++y) for (int x=0;x<2;++x)
        sum+=texture(shadow_map,vec3(uv.xy+(vec2(x,y)-0.5)*texel,uv.z-bias));
    return sum*0.25;
}
#endif
void main()
{
    if (covered_by_finer(relative_position)) discard;
#if !AX_WORLD_SHADOW
    vec3 n=normalize(surface_normal);
    float diffuse=max(dot(n,view.sun.xyz),0.0);
    if (diffuse>0.0) diffuse*=visibility(relative_position,n);
    float elevation=dot(n,view.local_up.xyz)*0.5+0.5;
    vec3 ambient=mix(vec3(0.17,0.16,0.18),vec3(0.32,0.37,0.45),elevation);
    vec3 lit=pow(albedo,vec3(2.2))*(ambient+vec3(1.0,0.94,0.84)*diffuse*1.15);
    float distance=length(relative_position);
    float density=view.sun.w>0.0 ? exp(-max(view.local_up.w,0.0)/view.sun.w)*0.00002 : 0.0;
    vec3 color=mix(lit,pow(sky_color(normalize(relative_position)),vec3(2.2)),min(0.6,1.0-exp(-distance*density)));
    out_color=vec4(pow(clamp(color,0.0,1.0),vec3(1.0/2.2)),1.0);
#endif
}
