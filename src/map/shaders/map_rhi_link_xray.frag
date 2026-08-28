#version 440

layout(location = 0) in vec4 vertex_color;
layout(location = 1) in vec2 segment_local_px;
layout(location = 2) in float segment_length_px;
layout(location = 3) in float segment_half_width_px;
layout(location = 0) out vec4 fragment_color;

void main()
{
    if (segment_half_width_px <= 0.0)
        discard;

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

    // A screen-stable broken stroke marks actual underground geometry while
    // preserving the symbology hue. It remains readable through terrain but
    // cannot be mistaken for an exposed solid pipe.
    float dash = mod(floor((segment_local_px.x + 2.0) / 7.0), 2.0);
    if (dash > 0.5)
        discard;

    float alpha = vertex_color.a * coverage * 0.62;
    if (alpha <= 0.001)
        discard;

    fragment_color = vec4(vertex_color.rgb * alpha, alpha);
}
