#version 440

layout(location = 0) in vec3 world_position;

layout(std140, binding = 0) uniform CameraBlock
{
    mat4 view_projection;
    vec4 viewport_and_sizes;
    vec4 heatmap_settings;
    vec4 basemap_settings;
    vec4 network_translation;
} camera;

void main()
{
    gl_Position = camera.view_projection * vec4(world_position, 1.0);
}
