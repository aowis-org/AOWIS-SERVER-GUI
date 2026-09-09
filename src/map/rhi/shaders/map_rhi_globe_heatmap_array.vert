#version 440

layout(location = 0) in vec3 world_position;
layout(location = 1) in vec2 texture_coordinate;
layout(location = 2) in float imagery_texture_layer;
layout(location = 3) in float heatmap_texture_layer;

layout(std140, binding = 0) uniform GlobeCameraBlock
{
    mat4 view_projection;
    vec4 heatmap_settings;
    vec4 basemap_settings;
} camera;

layout(location = 0) out vec2 vertex_texture_coordinate;
layout(location = 1) flat out float vertex_imagery_texture_layer;
layout(location = 2) flat out float vertex_heatmap_texture_layer;

void main()
{
    gl_Position = camera.view_projection * vec4(world_position, 1.0);
    vertex_texture_coordinate = texture_coordinate;
    vertex_imagery_texture_layer = imagery_texture_layer;
    vertex_heatmap_texture_layer = heatmap_texture_layer;
}
