#version 440

layout(location = 0) in vec4 vertex_color;
layout(location = 1) in vec2 node_corner;
layout(location = 0) out vec4 fragment_color;

void main()
{
    if (dot(node_corner, node_corner) > 1.0)
        discard;

    fragment_color = vertex_color;
}
