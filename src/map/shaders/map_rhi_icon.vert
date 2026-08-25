#version 440

layout(location = 0) in vec3 center_position;
layout(location = 1) in vec2 offset_pixels;
layout(location = 2) in vec2 texture_coordinate;
layout(location = 3) in vec4 color;

layout(std140, binding = 0) uniform CameraBlock
{
    mat4 view_projection;
    vec4 viewport_and_sizes;
    vec4 heatmap_settings;
    vec4 basemap_settings;
    vec4 network_translation;
} camera;

layout(location = 0) out vec2 vertex_texture_coordinate;
layout(location = 1) out vec4 vertex_color;

void main()
{
    vec4 clip_position = camera.view_projection * vec4(center_position, 1.0);
    vec2 viewport = max(camera.viewport_and_sizes.xy, vec2(1.0));
    vec2 translation_ndc = camera.network_translation.xy * 2.0 / viewport;
    clip_position.xy += translation_ndc * clip_position.w;
    vec2 offset_ndc = offset_pixels * 2.0 / viewport;
    clip_position.xy += offset_ndc * clip_position.w;

    gl_Position = clip_position;
    vertex_texture_coordinate = texture_coordinate;
    vertex_color = color;
}
