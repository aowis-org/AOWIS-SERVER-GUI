#version 440

layout(location = 0) in vec4 vertex_color;
layout(location = 1) in vec2 node_corner;
layout(location = 0) out vec4 fragment_color;

void main()
{
    float radius_squared = dot(node_corner, node_corner);
    float edge_width = max(fwidth(radius_squared), 0.0001);
    float coverage = 1.0 - smoothstep(
        1.0 - edge_width, 1.0 + edge_width, radius_squared);
    float alpha = vertex_color.a * coverage;
    if (alpha <= 0.001)
        discard;

    fragment_color = vec4(vertex_color.rgb * alpha, alpha);
}
