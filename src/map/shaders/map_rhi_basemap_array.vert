#version 440

layout(location = 0) in vec3 world_position;
layout(location = 1) in vec2 texture_coordinate;
layout(location = 2) in float texture_layer;

layout(std140, binding = 0) uniform CameraBlock
{
    mat4 view_projection;
    vec4 viewport_and_sizes;
    vec4 heatmap_settings;
    vec4 basemap_settings;
    vec4 network_translation;
} camera;

layout(location = 0) out vec2 vertex_texture_coordinate;
layout(location = 1) flat out float vertex_texture_layer;

void main()
{
    gl_Position = camera.view_projection * vec4(world_position, 1.0);
    vertex_texture_coordinate = texture_coordinate;
    vertex_texture_layer = texture_layer;
}
