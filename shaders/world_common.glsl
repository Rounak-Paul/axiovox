struct Region { vec4 offset_extent; uvec4 identity; };
layout(std430,set=0,binding=0) readonly buffer Regions { Region items[]; } regions;
layout(push_constant) uniform View {
    vec4 forward;
    vec4 right;
    vec4 up;
    vec4 sun;
    vec4 local_up;
    vec4 light_right;
    vec4 light_up;
    uvec4 info;
} view;

vec3 sky_color(vec3 d)
{
    if ((view.info.w>>1)!=0u) return vec3(0.018,0.022,0.03);
    float elevation=dot(d,view.local_up.xyz);
    float atmosphere=view.sun.w>0.0 ? exp(-max(view.local_up.w,0.0)/view.sun.w) : 0.0;
    float daylight=clamp(dot(view.local_up.xyz,view.sun.xyz)*2.0+0.2,0.025,1.0);
    vec3 sky=mix(vec3(0.78,0.84,0.88),vec3(0.32,0.52,0.78),clamp(elevation*1.4,0.0,1.0))*daylight;
    vec3 cell=floor(d*1600.0);
    float star=step(0.9985,fract(sin(dot(cell,vec3(12.9898,78.233,37.719)))*43758.5453));
    vec3 color=mix(vec3(0.002,0.003,0.008)+star*0.65,sky,atmosphere);
    return color+vec3(1.0,0.9,0.7)*pow(max(dot(d,view.sun.xyz),0.0),900.0)*4.0;
}
