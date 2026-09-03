#version 440

layout(location = 0) in vec3 vertex_normal;
layout(location = 1) in vec4 vertex_color;
layout(location = 2) in float vertex_selected;
layout(location = 0) out vec4 fragment_color;

void main()
{
    // Junction symbology colors are data visualization, not a material.
    // Preserve the selected palette color exactly in 3D instead of changing
    // it with lighting, shininess, edge darkening, or other material effects.
    fragment_color = vertex_color;
}
