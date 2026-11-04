#version 450
#extension GL_GOOGLE_include_directive : require
#include "world_common.glsl"
layout(location=0) in vec2 ndc;
layout(location=0) out vec4 out_color;
layout(set=0,binding=1) uniform sampler2DShadow shadow_map;
layout(set=0,binding=4) uniform sampler2D opaque_color;
layout(set=0,binding=5) uniform sampler2D opaque_depth;
layout(std430,set=0,binding=6) readonly buffer Ocean { vec4 eye_radius; vec4 radial_altitude; vec4 phase_time; } ocean;

/** Converts infinite reverse-Z depth to distance along a normalized camera ray. */
float scene_distance(vec2 uv,vec3 ray)
{
    float depth=texture(opaque_depth,uv).r;
    return depth>0.0 ? view.up.w/(depth*max(dot(ray,view.forward.xyz),0.00001)) : 1e20;
}

/** Hash and interpolated noise match the source water material's procedural domain. */
float hash21(vec2 p)
{
    vec3 q=fract(vec3(p.xyx)*0.1031);
    q+=dot(q,q.yzx+33.33);
    return fract((q.x+q.y)*q.z);
}
/** Returns signed smooth value noise for wave distortion and foam breakup. */
float value_noise(vec2 p)
{
    vec2 i=floor(p),f=fract(p),u=f*f*(3.0-2.0*f);
    return mix(mix(hash21(i),hash21(i+vec2(1,0)),u.x),
        mix(hash21(i+vec2(0,1)),hash21(i+vec2(1,1)),u.x),u.y)*2.0-1.0;
}
/** One choppy wave octave, with noise-distorted counter-flowing crests. */
float wave_octave(vec2 uv,float choppy)
{
    uv+=value_noise(uv);
    vec2 wave=1.0-abs(sin(uv));
    wave=mix(wave,abs(cos(uv)),wave);
    return pow(max(0.0,1.0-pow(wave.x*wave.y,0.65)),choppy);
}
/** Evaluates the two broad choppy wave octaves on a planar projection. */
float wave_plane(vec2 p,float time,float footprint)
{
    vec2 uv=p*vec2(0.75,1.0),flow=normalize(vec2(1.0,0.25))*time*1.5;
    float sum=0.0,amplitude=1.3,frequency=0.025,choppy=2.5;
    for (int i=0;i<2;++i) {
        float attenuation=1.0-smoothstep(0.5,3.0,footprint*frequency);
        if (attenuation<=0.0) break;
        float pair=wave_octave((uv+flow)*frequency,choppy)+wave_octave((uv-flow)*frequency,choppy);
        sum+=(pair-1.0)*amplitude*attenuation;
        uv=mat2(1.6,1.2,-1.2,1.6)*uv;
        frequency*=1.9; amplitude*=0.14; footprint*=2.0;
        choppy=mix(choppy,1.0,0.2);
    }
    return sum;
}
/** Blends projections around the sphere, avoiding a planar pole or longitude seam. */
float wave_height(vec3 p,vec3 weights,float footprint)
{
    float time=ocean.phase_time.w,result=0.0;
    for (int i=0;i<3;++i) {
        if (weights[i]<0.001) continue;
        vec2 uv=i==0 ? p.zy : i==1 ? p.xz : p.xy;
        float h=wave_plane(uv,time,footprint);
        if (time>4088.0) h=mix(h,wave_plane(uv,time-4096.0,footprint),smoothstep(4088.0,4096.0,time));
        result+=weights[i]*h;
    }
    return result;
}

vec3 ocean_color()
{
    vec2 uv=ndc*0.5+0.5;
    vec3 background=texture(opaque_color,uv).rgb;

    vec3 ray=normalize(view.forward.xyz+view.right.xyz*ndc.x*view.forward.w*view.right.w-view.up.xyz*ndc.y*view.forward.w);
    float radius=ocean.eye_radius.w,altitude=ocean.radial_altitude.w;
    float b=dot(ocean.eye_radius.xyz,ray);
    float c=altitude*(2.0*radius+altitude);
    float discriminant=b*b-c;
    if (discriminant<0.0 || (altitude>=0.0 && b>=0.0)) return background;
    float root=sqrt(max(0.0,discriminant));
    float surface=altitude>=0.0 ? c/max(-b+root,0.00001) : -b+root;
    vec3 p=ray*surface;
    vec3 radial=normalize(ocean.radial_altitude.xyz*(radius+altitude)+p);
    float footprint=surface*view.forward.w*2.0/float(textureSize(opaque_depth,0).y);
    vec3 weights=pow(abs(radial),vec3(4.0)); weights/=dot(weights,vec3(1));
    vec3 phase=radial*radius;
    float height=wave_height(phase,weights,footprint);
    float incidence=dot(ray,radial);
    surface=max(0.0,surface+height*incidence/(incidence*incidence+0.0025));
    p=ray*surface;
    float terrain=scene_distance(uv,ray);
    bool underwater=altitude<0.0;
    if (!underwater && terrain<=surface) return background;
    float thickness=underwater ? min(terrain,surface) : max(0.0,terrain-surface);
    vec3 absorption=vec3(0.3,0.06,0.02);
    vec3 transmission=exp(-absorption*min(thickness,600.0));
    float daylight=max(dot(radial,view.sun.xyz),0.0);
    vec3 deep=pow(vec3(0.0,0.341,0.29),vec3(2.2));
    vec3 shallow=pow(vec3(0.439,0.973,1.0),vec3(2.2));
    deep=mix(deep,vec3(0.005,0.026,0.075),1.0-exp(-thickness/35.0));
    vec3 scattering=mix(shallow,deep,sqrt(1.0-exp(-min(thickness,60.0)*0.32)))*(0.12+daylight);
    if (underwater && terrain<surface) {
        return pow(clamp(pow(background,vec3(2.2))*transmission+scattering*(1.0-transmission),0.0,1.0),vec3(1.0/2.2));
    }
    vec3 tangent=normalize(cross(radial,abs(radial.y)<0.9 ? vec3(0,1,0) : vec3(1,0,0)));
    vec3 bitangent=cross(radial,tangent);
    float epsilon=max(0.5,footprint);
    float dx=(wave_height(phase+tangent*epsilon,weights,footprint)-height)/epsilon;
    float dy=(wave_height(phase+bitangent*epsilon,weights,footprint)-height)/epsilon;
    vec3 normal=normalize(radial-0.5*(tangent*dx+bitangent*dy));
    float crest=clamp(height/5.2+0.5,0.0,1.0);
    if (underwater) normal=-normal;
    float facing=max(dot(normal,-ray),0.0);
    float fresnel=0.02037+0.97963*pow(1.0-facing,5.0);
    if (underwater && facing<0.6593) fresnel=1.0;
    vec3 reflection=pow(clamp(sky_color(reflect(ray,normal)),0.0,1.0),vec3(2.2));
    float shadow=1.0;
    vec2 light=light_plane(p)*vec2(0.5,-0.5)+0.5;
    float z=light_depth(p)-0.0002;
    if ((view.info.w&1u)!=0u && all(greaterThan(light,vec2(0.001))) && all(lessThan(light,vec2(0.999))) && z>0.0 && z<1.0)
        shadow=texture(shadow_map,vec3(light,z));
    vec3 half_delta=view.sun.xyz-ray;
    vec3 half_vector=half_delta/max(length(half_delta),0.00001);
    float specular=pow(max(dot(normal,half_vector),0.0),256.0)*daylight*shadow;
    vec2 offset=vec2(dot(normal,view.right.xyz),-dot(normal,view.up.xyz))*0.014*min(1.0,5.0/max(surface,1.0))*smoothstep(0.0,8.0,thickness);
    vec2 refracted_uv=clamp(uv+offset,vec2(0.0),vec2(1.0));
    vec2 refracted_ndc=refracted_uv*2.0-1.0;
    vec3 refracted_ray=normalize(view.forward.xyz+view.right.xyz*refracted_ndc.x*view.forward.w*view.right.w-view.up.xyz*refracted_ndc.y*view.forward.w);
    if (scene_distance(refracted_uv,refracted_ray)<=surface) refracted_uv=uv;
    vec3 seabed=pow(texture(opaque_color,refracted_uv).rgb,vec3(2.2));
    float caustic=pow(max(0.0,1.0-abs(sin(dot(phase,vec3(0.04,0.02,0.06))+ocean.phase_time.w*0.2945243))),8.0);
    seabed*=1.0+0.6*caustic*exp(-thickness/3.0)*daylight*shadow;
    vec3 transmitted=seabed*transmission+scattering*(1.0-transmission);
    float shore_depth=thickness*abs(incidence);
    float shore=1.0-smoothstep(0.0,1.3*(0.75+0.25*sin(ocean.phase_time.w*1.3008157)),shore_depth);
    float foam=max(shore*smoothstep(0.18,0.55,0.35+value_noise(phase.xz*0.25+vec2(0.05,-0.03)*ocean.phase_time.w)),smoothstep(0.72,0.92,crest)*smoothstep(0.3,0.7,value_noise(phase.xz*0.035))*0.3);
    vec3 color=mix(transmitted,reflection,fresnel)+vec3(1.0,0.94,0.84)*specular;
    color=mix(color,pow(vec3(0.94,0.98,1.0),vec3(2.2))*(0.15+daylight*shadow),foam);
    if (underwater) color=color*transmission+scattering*(1.0-transmission);
    float density=view.sun.w>0.0 ? exp(-max(view.local_up.w,0.0)/view.sun.w)*0.00002 : 0.0;
    color=mix(color,pow(clamp(sky_color(ray),0.0,1.0),vec3(2.2)),min(0.6,1.0-exp(-surface*density)));
    return pow(clamp(color,0.0,1.0),vec3(1.0/2.2));
}

/** Integrates the planet's thin atmospheric shell for orbital views. */
void main()
{
    vec3 color=ocean_color();
    float radius=ocean.eye_radius.w,altitude=ocean.radial_altitude.w;
    float orbital=smoothstep(radius*0.015,radius*0.06,altitude);
    if (orbital>0.0) {
        vec3 ray=normalize(view.forward.xyz+view.right.xyz*ndc.x*view.forward.w*view.right.w-view.up.xyz*ndc.y*view.forward.w);
        float b=dot(ocean.eye_radius.xyz,ray),shell=radius*0.018;
        float discriminant=b*b-(altitude-shell)*(2.0*radius+altitude+shell);
        if (discriminant>0.0) {
            float start=max(0.0,-b-sqrt(discriminant)),end=-b+sqrt(discriminant);
            end=min(end,scene_distance(ndc*0.5+0.5,ray));
            float solid=b*b-altitude*(2.0*radius+altitude);
            if (solid>=0.0 && b<0.0) end=min(end,altitude*(2.0*radius+altitude)/max(-b+sqrt(solid),0.00001));
            float step_length=max(0.0,end-start)/6.0,optical=0.0,lit=0.0;
            for (int i=0;i<6;++i) {
                vec3 position=ocean.eye_radius.xyz+ray*(start+(float(i)+0.5)*step_length);
                float height=max(0.0,length(position)-radius);
                float density=exp(-height/(radius*0.004))*step_length;
                optical+=density;
                lit+=density*smoothstep(-0.12,0.15,dot(normalize(position),view.sun.xyz));
            }
            vec3 transmission=exp(-optical*vec3(0.000006,0.000014,0.000032));
            float sun_angle=dot(ray,view.sun.xyz);
            float phase=0.75*(1.0+sun_angle*sun_angle);
            vec3 scatter=(1.0-transmission)*phase*(lit/max(optical,0.00001));
            vec3 atmosphere=pow(clamp(pow(color,vec3(2.2))*transmission+scatter,0.0,1.0),vec3(1.0/2.2));
            color=mix(color,atmosphere,orbital);
        }
    }
    out_color=vec4(color,1.0);
}
