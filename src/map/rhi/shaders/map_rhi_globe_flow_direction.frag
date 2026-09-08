#version 440

layout(location = 0) flat in vec4 vertex_color;
layout(location = 1) flat in vec4 screen_basis;
layout(location = 2) flat in vec4 screen_metrics;
layout(location = 0) out vec4 fragment_color;

void main()
{
    float segment_length_px = screen_metrics.x;
    float segment_half_width_px = screen_metrics.y;
    if (segment_half_width_px <= 0.0)
        discard;

    vec2 tangent = screen_basis.zw;
    vec2 normal = vec2(-tangent.y, tangent.x);
    vec2 fragment_from_start = gl_FragCoord.xy / screen_metrics.zw
        - screen_basis.xy;
    vec2 segment_local_px = vec2(
        dot(fragment_from_start, tangent),
        dot(fragment_from_start, normal));
    float nearest_x = clamp(segment_local_px.x, 0.0, segment_length_px);
    vec2 to_segment = vec2(
        segment_local_px.x - nearest_x,
        segment_local_px.y);
    float distance_to_segment = length(to_segment);
    float edge_width = max(fwidth(distance_to_segment), 0.5);
    float coverage = 1.0 - smoothstep(
        segment_half_width_px - edge_width,
        segment_half_width_px + edge_width,
        distance_to_segment);
    float alpha = vertex_color.a * coverage;
    if (alpha <= 0.001)
        discard;

    fragment_color = vec4(vertex_color.rgb * alpha, alpha);
}
