#version 440

layout(binding = 1) uniform sampler2D icon_atlas;
layout(location = 0) in vec2 vertex_texture_coordinate;
layout(location = 1) in vec4 vertex_color;
layout(location = 0) out vec4 fragment_color;

void main()
{
    float coverage = texture(icon_atlas, vertex_texture_coordinate).a;
    float alpha = vertex_color.a * coverage;
    if (alpha <= 0.001)
        discard;

    fragment_color = vec4(vertex_color.rgb * alpha, alpha);
}
