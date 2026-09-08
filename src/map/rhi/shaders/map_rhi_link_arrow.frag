#version 440

layout(location = 0) flat in vec4 arrow_color_and_direction;
layout(location = 1) flat in vec4 screen_basis;
layout(location = 2) flat in vec4 segment_and_link_metrics;
layout(location = 3) flat in vec4 arrow_metrics;
layout(location = 0) out vec4 fragment_color;

float distanceToSegment(vec2 point, vec2 start_point, vec2 end_point)
{
    vec2 segment = end_point - start_point;
    float segment_length_squared = dot(segment, segment);
    if (segment_length_squared <= 0.000001)
        return length(point - start_point);

    float ratio = clamp(
        dot(point - start_point, segment) / segment_length_squared,
        0.0, 1.0);
    return length(point - (start_point + segment * ratio));
}

void main()
{
    const float minimum_link_pixels = 18.0;
    const float marker_spacing_pixels = 100.0;
    const float maximum_marker_count = 32.0;

    float flow_direction = arrow_color_and_direction.a;
    float segment_length_px = segment_and_link_metrics.x;
    float segment_length_world = segment_and_link_metrics.y;
    float cumulative_distance = segment_and_link_metrics.z;
    float encoded_total_length = segment_and_link_metrics.w;
    float pixels_per_meter = arrow_metrics.x;
    float arrow_size_px = arrow_metrics.y;
    float output_pixels_per_logical_pixel = arrow_metrics.z;
    float total_length = abs(encoded_total_length);
    float total_screen_length = total_length * pixels_per_meter;
    if (abs(flow_direction) < 0.5
        || arrow_size_px <= 0.0
        || pixels_per_meter <= 0.0
        || total_length <= 0.0
        || segment_length_px <= 0.0001
        || segment_length_world <= 0.0001
        || total_screen_length < minimum_link_pixels)
    {
        discard;
    }

    vec2 tangent = screen_basis.zw;
    vec2 normal = vec2(-tangent.y, tangent.x);
    vec2 fragment_from_start = gl_FragCoord.xy
        / output_pixels_per_logical_pixel - screen_basis.xy;
    float fragment_along_px = dot(fragment_from_start, tangent);
    float fragment_side_px = dot(fragment_from_start, normal);
    float clamped_segment_ratio = clamp(
        fragment_along_px / segment_length_px, 0.0, 1.0);
    float fragment_distance = cumulative_distance
        + clamped_segment_ratio * segment_length_world;

    // Anchor the marker lattice at the link start. Changing the number of
    // visible markers now only introduces/removes the final marker; it no
    // longer redistributes every existing marker along the link. Clamping
    // the spacing at total/32 keeps the cap transition continuous too.
    float marker_spacing_world = max(
        marker_spacing_pixels / pixels_per_meter,
        total_length / maximum_marker_count);
    float marker_density = total_length / marker_spacing_world;
    float marker_count = clamp(
        floor(marker_density + 0.5), 1.0, maximum_marker_count);
    float target_distance;
    if (encoded_total_length < 0.0)
    {
        // Pumps and valves intentionally retain their single, stable marker.
        target_distance = total_length * 0.3;
    }
    else if (marker_density < 1.0)
    {
        target_distance = total_length * 0.5;
    }
    else
    {
        float marker_index = clamp(
            floor(fragment_distance / marker_spacing_world),
            0.0, marker_count - 1.0);
        target_distance = (marker_index + 0.5) * marker_spacing_world;
    }

    // The segment ending at a polyline join owns a marker exactly on that
    // join. This prevents the adjacent instance from drawing it twice.
    if (target_distance <= cumulative_distance
        || target_distance > cumulative_distance + segment_length_world)
    {
        discard;
    }

    float marker_center_px = (target_distance - cumulative_distance)
        * segment_length_px / segment_length_world;
    vec2 marker_position = vec2(
        fragment_along_px - marker_center_px,
        fragment_side_px);
    if (flow_direction < 0.0)
        marker_position.x = -marker_position.x;

    float half_length_px = arrow_size_px * 0.5;
    float half_width_px = arrow_size_px * 0.4;
    float half_stroke_px = max(0.5, arrow_size_px * 0.1);
    if (abs(marker_position.x) > half_length_px + half_stroke_px + 1.0
        || abs(marker_position.y) > half_width_px + half_stroke_px + 1.0)
    {
        discard;
    }

    vec2 tip = vec2(half_length_px, 0.0);
    vec2 first_tail = vec2(-half_length_px, half_width_px);
    vec2 second_tail = vec2(-half_length_px, -half_width_px);
    float distance_to_chevron = min(
        distanceToSegment(marker_position, first_tail, tip),
        distanceToSegment(marker_position, second_tail, tip));
    float edge_width = max(fwidth(distance_to_chevron), 0.5);
    float coverage = 1.0 - smoothstep(
        half_stroke_px - edge_width,
        half_stroke_px + edge_width,
        distance_to_chevron);

    // At a density boundary the new final marker starts exactly at the link
    // endpoint. Fade it in as it moves inward, so a marker-count transition
    // cannot flash for a frame while panning or orbiting.
    if (encoded_total_length >= 0.0 && marker_count > 1.5)
    {
        float endpoint_distance_px = min(
            target_distance, total_length - target_distance)
            * pixels_per_meter;
        coverage *= smoothstep(
            0.0, max(arrow_size_px, 1.0), endpoint_distance_px);
    }
    if (coverage <= 0.001)
        discard;

    fragment_color = vec4(
        arrow_color_and_direction.rgb * coverage,
        coverage);
}
