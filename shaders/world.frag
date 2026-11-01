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
float visibility(vec3 uv,vec2 gradient)
{
    if (any(lessThan(uv,vec3(0.001))) || any(greaterThan(uv,vec3(0.999)))) return 1.0;
    vec2 size=vec2(textureSize(shadow_map,0));
    float sum=0.0;
    for (int y=0;y<2;++y) for (int x=0;x<2;++x) {
        vec2 tap=(floor(uv.xy*size+vec2(x,y)-0.5)+0.5)/size;
        float receiver=uv.z+dot(gradient,tap-uv.xy)-0.000015;
        sum+=texture(shadow_map,vec3(tap,receiver));
    }
    return sum*0.25;
}
#endif
void main()
{
#if !AX_WORLD_SHADOW
    float span=view.light_right.w;
    vec3 uv=vec3(dot(relative_position,view.light_right.xyz)/span*0.5+0.5,
        -dot(relative_position,view.light_up.xyz)/span*0.5+0.5,0.5-dot(relative_position,view.sun.xyz)/(span*4.0));
    vec3 dx=dFdx(uv),dy=dFdy(uv);
    float determinant=dx.x*dy.y-dx.y*dy.x;
    vec2 gradient=abs(determinant)>1e-12 ? vec2(dx.z*dy.y-dy.z*dx.y,dy.z*dx.x-dx.z*dy.x)/determinant : vec2(0.0);
    vec3 n=normalize(surface_normal);
    uint mode=view.info.w>>1;
    if (mode!=0u) { out_color=vec4(mode==2u ? n*0.5+0.5 : vec3(0.8,0.9,1.0),1.0); return; }
    float diffuse=max(dot(n,view.sun.xyz),0.0);
    if (diffuse>0.0 && (view.info.w&1u)!=0u) diffuse*=visibility(uv,gradient);
    float elevation=dot(n,view.local_up.xyz)*0.5+0.5;
    vec3 ambient=mix(vec3(0.17,0.16,0.18),vec3(0.32,0.37,0.45),elevation);
    vec3 lit=pow(albedo,vec3(2.2))*(ambient+vec3(1.0,0.94,0.84)*diffuse*1.15);
    float distance=length(relative_position);
    float density=view.sun.w>0.0 ? exp(-max(view.local_up.w,0.0)/view.sun.w)*0.00002 : 0.0;
    vec3 color=mix(lit,pow(sky_color(normalize(relative_position)),vec3(2.2)),min(0.6,1.0-exp(-distance*density)));
    out_color=vec4(pow(clamp(color,0.0,1.0),vec3(1.0/2.2)),1.0);
#endif
}
