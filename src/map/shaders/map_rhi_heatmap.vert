#version 440

layout(location = 0) in vec3 center_position;
layout(location = 1) in vec2 corner;
layout(location = 2) in vec3 color;

layout(std140, binding = 0) uniform CameraBlock
{
    mat4 view_projection;
    vec4 viewport_and_sizes;
    vec4 heatmap_settings;
    vec4 basemap_settings;
    vec4 network_translation;
} camera;

layout(location = 0) out vec2 heatmap_corner;
layout(location = 1) out vec3 heatmap_color;

void main()
{
    float radius_world = max(camera.heatmap_settings.w, 0.000001);
    vec3 world_position = center_position + vec3(corner * radius_world, 0.0);
    vec4 clip_position = camera.view_projection * vec4(world_position, 1.0);
    vec2 viewport = max(camera.viewport_and_sizes.xy, vec2(1.0));
    vec2 translation_ndc = camera.network_translation.xy * 2.0 / viewport;
    clip_position.xy += translation_ndc * clip_position.w;

    gl_Position = clip_position;
    heatmap_corner = corner;
    heatmap_color = color;
}
