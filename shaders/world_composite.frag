#version 450
layout(location=0) in vec2 ndc;
layout(location=0) out vec4 out_color;
layout(set=0,binding=2) uniform sampler2D scene_color;
void main()
{
    out_color=texture(scene_color,ndc*0.5+0.5);
}
