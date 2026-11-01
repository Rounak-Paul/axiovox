#version 450
layout(location=0) in vec2 ndc;
layout(location=0) out vec4 out_color;
layout(set=0,binding=2) uniform sampler2D scene_color;
layout(set=0,binding=3) uniform sampler2D previous_color;
layout(push_constant) uniform Composite { float previous_weight; } composite;
void main()
{
    float dither=fract(52.9829189*fract(dot(gl_FragCoord.xy,vec2(0.06711056,0.00583715))))-0.5;
    vec2 uv=ndc*0.5+0.5;
    vec3 color=texture(scene_color,uv).rgb;
    if (composite.previous_weight>0.0) color=mix(color,texture(previous_color,uv).rgb,composite.previous_weight);
    out_color=vec4(color+dither/255.0,1.0);
}
