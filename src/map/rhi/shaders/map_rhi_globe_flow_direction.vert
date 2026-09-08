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
    vec4 camera_right;
    vec4 camera_up;
    vec4 camera_eye;
    vec4 impostor_settings;
} camera;

// Each six-vertex stroke repeats its endpoints and style, so these values
// are genuinely constant across both triangles. Reconstructing the capsule
// from this flat screen basis avoids perspective-interpolating pixel-space
// distances while the Globe camera pans and orbits.
layout(location = 0) flat out vec4 vertex_color;
layout(location = 1) flat out vec4 screen_basis;
layout(location = 2) flat out vec4 screen_metrics;

vec3 depthSeparatedPosition(
    vec3 position, float view_depth, vec2 viewport)
{
    // Pull each endpoint toward the eye along its own projection ray. This
    // changes depth but mathematically preserves its NDC X/Y, so the existing
    // surface-aligned 3D chevron remains visually in exactly the same place.
    // Expressing the separation in logical pixels keeps it strong enough at
    // every Globe distance instead of relying on a couple of depth-buffer
    // units whose world-space meaning varies drastically with perspective.
    const float tan_half_fov = 0.4142135623730950;
    const float depth_separation_pixels = 8.0;
    vec3 direction_to_eye = camera.camera_eye.xyz - position;
    float distance_to_eye = length(direction_to_eye);
    if (distance_to_eye <= 0.000001)
        return position;

    float world_units_per_pixel =
        2.0 * max(abs(view_depth), 0.000001) * tan_half_fov / viewport.y;
    return position + direction_to_eye / distance_to_eye
        * (world_units_per_pixel * depth_separation_pixels);
}

void main()
{
    vec2 viewport = max(camera.viewport_and_sizes.xy, vec2(1.0));
    vec4 unseparated_start_clip =
        camera.view_projection * vec4(start_position, 1.0);
    vec4 unseparated_end_clip =
        camera.view_projection * vec4(end_position, 1.0);
    vec3 separated_start_position = depthSeparatedPosition(
        start_position, unseparated_start_clip.w, viewport);
    vec3 separated_end_position = depthSeparatedPosition(
        end_position, unseparated_end_clip.w, viewport);
    vec4 start_clip = camera.view_projection
        * vec4(separated_start_position, 1.0);
    vec4 end_clip = camera.view_projection
        * vec4(separated_end_position, 1.0);
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

    float configured_half_width = camera.viewport_and_sizes.z;
    float half_width = max(configured_half_width, 0.0);
    if (configured_half_width < 0.0)
    {
        float half_width_world = -configured_half_width;
        vec3 world_direction = end_position - start_position;
        vec2 world_direction_xy = world_direction.xy;
        float world_direction_length = length(world_direction_xy);
        vec2 world_normal = world_direction_length > 0.000001
            ? vec2(-world_direction_xy.y, world_direction_xy.x)
                / world_direction_length
            : vec2(1.0, 0.0);
        vec3 midpoint = mix(start_position, end_position, 0.5);
        vec4 midpoint_clip = camera.view_projection * vec4(midpoint, 1.0);
        vec4 width_clip = camera.view_projection
            * vec4(midpoint + vec3(world_normal * half_width_world, 0.0), 1.0);
        vec2 midpoint_ndc = midpoint_clip.xy / midpoint_clip.w;
        vec2 width_ndc = width_clip.xy / width_clip.w;
        half_width = length((width_ndc - midpoint_ndc) * viewport * 0.5);
    }
    if (configured_half_width < 0.0 && size_adjust_px < 0.0)
        half_width = -size_adjust_px;
    else
        half_width = max(half_width + size_adjust_px, 0.0);

    float raster_margin = 1.0;
    float extent = half_width + raster_margin;
    float endpoint_sign = corner.x * 2.0 - 1.0;
    vec4 clip_position = mix(start_clip, end_clip, corner.x);
    vec2 offset_pixels = tangent * endpoint_sign * extent
        + normal * corner.y * extent;
    clip_position.xy += offset_pixels * 2.0 / viewport * clip_position.w;

    vec2 start_pixel = (start_ndc * 0.5 + vec2(0.5)) * viewport;
    gl_Position = clip_position;
    vertex_color = color;
    screen_basis = vec4(start_pixel, tangent);
    screen_metrics = vec4(
        direction_length,
        half_width,
        max(camera.heatmap_settings.x, 1.0),
        max(camera.heatmap_settings.y, 1.0));
}
