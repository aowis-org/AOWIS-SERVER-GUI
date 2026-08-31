#version 440

layout(location = 0) in vec3 center_position;
layout(location = 1) in vec2 offset_ratio;
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

    float configured_size = camera.network_translation.w;
    float icon_size_px = max(configured_size, 0.0);
    bool perspective = abs(camera.view_projection[3][3]) < 0.5;
    if (configured_size < 0.0)
    {
        float icon_size_world = -configured_size;
        if (perspective)
        {
            const float tan_half_fov = 0.4142135623730950;
            float depth = max(abs(clip_position.w), 0.000001);
            icon_size_px = icon_size_world * viewport.y / (2.0 * depth * tan_half_fov);
        }
        else
        {
            vec4 edge_clip = camera.view_projection
                * vec4(center_position + vec3(icon_size_world, 0.0, 0.0), 1.0);
            edge_clip.xy += translation_ndc * edge_clip.w;
            vec2 center_ndc = clip_position.xy / clip_position.w;
            vec2 edge_ndc = edge_clip.xy / edge_clip.w;
            icon_size_px = length((edge_ndc - center_ndc) * viewport * 0.5);
        }
    }
    else if (perspective)
    {
        float reference_depth = max(camera.network_translation.z, 0.000001);
        float depth = max(abs(clip_position.w), 0.000001);
        icon_size_px *= reference_depth / depth;
    }

    vec2 offset_ndc = offset_ratio * icon_size_px * 2.0 / viewport;
    clip_position.xy += offset_ndc * clip_position.w;

    gl_Position = clip_position;
    vertex_texture_coordinate = texture_coordinate;
    vertex_color = color;
}
