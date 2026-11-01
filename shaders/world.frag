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
float visibility(vec3 uv)
{
    if (any(lessThan(uv,vec3(0.001))) || any(greaterThan(uv,vec3(0.999)))) return 1.0;
    vec2 size=vec2(textureSize(shadow_map,0));
    vec2 texel=uv.xy*size-0.5,base=floor(texel),blend=texel-base;
    float receiver=uv.z-0.0002;
    float low=mix(texture(shadow_map,vec3((base+vec2(0.5,0.5))/size,receiver)),texture(shadow_map,vec3((base+vec2(1.5,0.5))/size,receiver)),blend.x);
    float high=mix(texture(shadow_map,vec3((base+vec2(0.5,1.5))/size,receiver)),texture(shadow_map,vec3((base+vec2(1.5,1.5))/size,receiver)),blend.x);
    vec2 edge=abs(uv.xy-0.5)*2.0;
    return mix(mix(low,high,blend.y),1.0,smoothstep(0.8,1.0,max(edge.x,edge.y)));
}
#endif
void main()
{
#if !AX_WORLD_SHADOW
    vec2 plane=light_plane(relative_position);
    vec3 uv=vec3(plane.x*0.5+0.5,-plane.y*0.5+0.5,light_depth(relative_position));
    vec3 n=normalize(surface_normal);
    uint mode=view.info.w>>1;
    if (mode!=0u) { out_color=vec4(mode==2u ? n*0.5+0.5 : vec3(0.8,0.9,1.0),1.0); return; }
    float diffuse=max(dot(n,view.sun.xyz),0.0);
    if (diffuse>0.0 && (view.info.w&1u)!=0u) diffuse*=visibility(uv);
    float elevation=dot(n,view.local_up.xyz)*0.5+0.5;
    vec3 ambient=mix(vec3(0.17,0.16,0.18),vec3(0.32,0.37,0.45),elevation);
    vec3 lit=pow(albedo,vec3(2.2))*(ambient+vec3(1.0,0.94,0.84)*diffuse*1.15);
    float distance=length(relative_position);
    float density=view.sun.w>0.0 ? exp(-max(view.local_up.w,0.0)/view.sun.w)*0.00002 : 0.0;
    vec3 color=mix(lit,pow(sky_color(normalize(relative_position)),vec3(2.2)),min(0.6,1.0-exp(-distance*density)));
    out_color=vec4(pow(clamp(color,0.0,1.0),vec3(1.0/2.2)),1.0);
#endif
}
