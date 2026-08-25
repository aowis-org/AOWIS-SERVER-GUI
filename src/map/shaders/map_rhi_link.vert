#version 440

layout(location = 0) in vec3 start_position;
layout(location = 1) in vec3 end_position;
layout(location = 2) in vec2 corner;
layout(location = 3) in vec4 color;
layout(location = 4) in float size_adjust_px;

layout(std140, binding = 0) uniform CameraBlock
{
    mat4 view_projection;
    vec4 viewport_and_sizes;
    vec4 heatmap_settings;
    vec4 basemap_settings;
    vec4 network_translation;
} camera;

layout(location = 0) out vec4 vertex_color;
layout(location = 1) out vec2 segment_local_px;
layout(location = 2) out float segment_length_px;
layout(location = 3) out float segment_half_width_px;

void main()
{
    vec4 start_clip = camera.view_projection * vec4(start_position, 1.0);
    vec4 end_clip = camera.view_projection * vec4(end_position, 1.0);
    vec2 viewport = max(camera.viewport_and_sizes.xy, vec2(1.0));
    vec2 translation_ndc = camera.network_translation.xy * 2.0 / viewport;
    start_clip.xy += translation_ndc * start_clip.w;
    end_clip.xy += translation_ndc * end_clip.w;
    vec2 start_ndc = start_clip.xy / start_clip.w;
    vec2 end_ndc = end_clip.xy / end_clip.w;
    vec2 direction_pixels = (end_ndc - start_ndc) * viewport * 0.5;
    float direction_length = length(direction_pixels);
    vec2 tangent = direction_length > 0.0001
        ? direction_pixels / direction_length
        : vec2(1.0, 0.0);
    vec2 normal = vec2(-tangent.y, tangent.x);

    float half_width = max(camera.viewport_and_sizes.z + size_adjust_px, 0.0);
    float raster_margin = 1.0;
    float extent = half_width + raster_margin;
    float endpoint_sign = corner.x * 2.0 - 1.0;

    vec4 clip_position = mix(start_clip, end_clip, corner.x);
    vec2 offset_pixels = tangent * endpoint_sign * extent
        + normal * corner.y * extent;
    vec2 offset_ndc = offset_pixels * 2.0 / viewport;
    clip_position.xy += offset_ndc * clip_position.w;

    gl_Position = clip_position;
    vertex_color = color;
    segment_local_px = vec2(
        mix(-extent, direction_length + extent, corner.x),
        corner.y * extent);
    segment_length_px = direction_length;
    segment_half_width_px = half_width;
}
