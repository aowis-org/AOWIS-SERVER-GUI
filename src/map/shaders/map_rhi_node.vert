#version 440

layout(location = 0) in vec3 center_position;
layout(location = 1) in vec2 corner;
layout(location = 2) in vec4 color;
layout(location = 3) in float size_adjust_px;

layout(std140, binding = 0) uniform CameraBlock
{
    mat4 view_projection;
    vec4 viewport_and_sizes;
    vec4 heatmap_settings;
    vec4 basemap_settings;
    vec4 network_translation;
} camera;

layout(location = 0) out vec4 vertex_color;
layout(location = 1) out vec2 node_corner;

void main()
{
    vec4 clip_position = camera.view_projection * vec4(center_position, 1.0);
    vec2 viewport = max(camera.viewport_and_sizes.xy, vec2(1.0));
    vec2 translation_ndc = camera.network_translation.xy * 2.0 / viewport;
    clip_position.xy += translation_ndc * clip_position.w;

    float configured_radius = camera.viewport_and_sizes.w;
    float radius_px = max(configured_radius, 0.0);
    if (configured_radius < 0.0)
    {
        float radius_world = -configured_radius;
        vec4 edge_clip = camera.view_projection
            * vec4(center_position + vec3(radius_world, 0.0, 0.0), 1.0);
        edge_clip.xy += translation_ndc * edge_clip.w;
        vec2 center_ndc = clip_position.xy / clip_position.w;
        vec2 edge_ndc = edge_clip.xy / edge_clip.w;
        radius_px = length((edge_ndc - center_ndc) * viewport * 0.5);
    }

    vec2 offset_ndc = corner * max(radius_px + size_adjust_px, 0.0) * 2.0 / viewport;
    clip_position.xy += offset_ndc * clip_position.w;

    gl_Position = clip_position;
    vertex_color = color;
    node_corner = corner;
}
