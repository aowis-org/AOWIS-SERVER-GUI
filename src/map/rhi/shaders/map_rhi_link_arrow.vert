#version 440

layout(location = 0) in vec2 corner;
layout(location = 1) in vec3 start_position;
layout(location = 2) in vec3 end_position;
layout(location = 3) in vec2 instance_distance_data;
layout(location = 4) in float instance_style_index;

layout(std140, binding = 0) uniform CameraBlock
{
    mat4 view_projection;
    vec4 viewport_and_sizes;
    // Globe reuses heatmap_settings.xyzw for flow-arrow pixels-per-meter,
    // chevron size, base-link half-width, and physical/logical output scale.
    // Its terrain renderer owns a separate uniform buffer.
    vec4 heatmap_settings;
    vec4 basemap_settings;
    vec4 network_translation;
    vec4 camera_right;
    vec4 camera_up;
    vec4 camera_eye;
    vec4 impostor_settings;
} camera;

layout(binding = 1) uniform sampler2D network_style_table;

// Every vertex of an instance writes the same values. Keeping the screen
// basis flat lets the fragment shader derive exact pixel coordinates from
// gl_FragCoord instead of perspective-interpolating pixel distances.
layout(location = 0) flat out vec4 arrow_color_and_direction;
layout(location = 1) flat out vec4 screen_basis;
layout(location = 2) flat out vec4 segment_and_link_metrics;
layout(location = 3) flat out vec4 arrow_metrics;

// Explicit dimensions keep this shader compatible with the ESSL 100 QSB
// target: textureSize() and texelFetch() are deliberately not used.
vec4 styleTexel(float style_index, float texel_offset)
{
    vec2 table_size = max(camera.impostor_settings.zw, vec2(1.0));
    float linear_index = style_index * 2.0 + texel_offset;
    float row = floor(linear_index / table_size.x);
    float column = linear_index - row * table_size.x;
    vec2 texture_coordinate =
        (vec2(column, row) + vec2(0.5)) / table_size;
    return texture(network_style_table, texture_coordinate);
}

void main()
{
    float pixels_per_meter = max(camera.heatmap_settings.x, 0.0);
    float arrow_size_px = max(camera.heatmap_settings.y, 0.0);
    vec4 style_color = styleTexel(instance_style_index, 0.0);
    vec4 style_state = styleTexel(instance_style_index, 1.0);
    float flow_direction = style_state.a > 0.75
        ? 1.0
        : (style_state.a < 0.25 ? -1.0 : 0.0);
    if (style_state.b <= 0.5)
        flow_direction = 0.0;

    float total_length = abs(instance_distance_data.y);
    bool arrow_drawable = abs(flow_direction) >= 0.5
        && arrow_size_px > 0.0
        && pixels_per_meter > 0.0
        && total_length * pixels_per_meter >= 18.0;
    if (!arrow_drawable)
    {
        // Collapse invisible/zero-flow/too-short instances before expensive
        // projection and rasterization. Flow-only updates therefore remain
        // a style-texture upload, never an instance-buffer rebuild.
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        arrow_color_and_direction = vec4(0.0);
        screen_basis = vec4(0.0);
        segment_and_link_metrics = vec4(0.0);
        arrow_metrics = vec4(0.0);
        return;
    }

    float elevation_px = max(4.0, camera.heatmap_settings.z + 2.0);
    float elevation_world = elevation_px / pixels_per_meter;
    vec3 midpoint = mix(start_position, end_position, 0.5);
    vec3 midpoint_to_eye = camera.camera_eye.xyz - midpoint;
    float midpoint_to_eye_length = length(midpoint_to_eye);
    vec3 lift = midpoint_to_eye_length > 0.000001
        ? midpoint_to_eye / midpoint_to_eye_length * elevation_world
        : vec3(0.0);
    vec3 lifted_start = start_position + lift;
    vec3 lifted_end = end_position + lift;

    vec4 start_clip = camera.view_projection * vec4(lifted_start, 1.0);
    vec4 end_clip = camera.view_projection * vec4(lifted_end, 1.0);
    vec2 viewport = max(camera.viewport_and_sizes.xy, vec2(1.0));
    vec2 start_ndc = start_clip.xy / start_clip.w;
    vec2 end_ndc = end_clip.xy / end_clip.w;
    vec2 direction_pixels = (end_ndc - start_ndc) * viewport * 0.5;
    float segment_length_px = length(direction_pixels);
    vec2 tangent = segment_length_px > 0.0001
        ? direction_pixels / segment_length_px
        : vec2(1.0, 0.0);
    vec2 normal = vec2(-tangent.y, tangent.x);
    vec2 start_pixel = (start_ndc * 0.5 + vec2(0.5)) * viewport;

    float half_stroke_px = max(0.5, arrow_size_px * 0.1);
    float along_extent_px = arrow_size_px * 0.5 + half_stroke_px + 1.0;
    float side_extent_px = arrow_size_px * 0.4 + half_stroke_px + 1.0;
    float endpoint_sign = corner.x * 2.0 - 1.0;

    vec4 clip_position = mix(start_clip, end_clip, corner.x);
    vec2 offset_pixels = tangent * endpoint_sign * along_extent_px
        + normal * corner.y * side_extent_px;
    clip_position.xy += offset_pixels * 2.0 / viewport * clip_position.w;

    float luminance = dot(
        style_color.rgb, vec3(0.2126, 0.7152, 0.0722));
    vec3 contrast_color = luminance >= 150.0 / 255.0
        ? vec3(0.0)
        : vec3(1.0);

    gl_Position = clip_position;
    arrow_color_and_direction = vec4(contrast_color, flow_direction);
    screen_basis = vec4(start_pixel, tangent);
    segment_and_link_metrics = vec4(
        segment_length_px,
        length(end_position - start_position),
        instance_distance_data.x,
        instance_distance_data.y);
    arrow_metrics = vec4(
        pixels_per_meter,
        arrow_size_px,
        max(camera.heatmap_settings.w, 1.0),
        0.0);
}
