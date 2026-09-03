#version 440

layout(binding = 1) uniform sampler2D globe_tile;

layout(location = 0) in vec2 vertex_texture_coordinate;
layout(location = 0) out vec4 fragment_color;

void main()
{
    fragment_color = vec4(texture(globe_tile, vertex_texture_coordinate).rgb, 1.0);
}
