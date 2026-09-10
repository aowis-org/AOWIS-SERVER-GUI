#version 440

layout(location = 0) in vec2 heatmap_corner;
layout(location = 1) flat in float heatmap_solid_fraction;
layout(location = 2) flat in vec3 heatmap_color;
layout(location = 0) out vec4 fragment_color;

void main()
{
    float distance_from_center = length(heatmap_corner);
    if (distance_from_center >= 1.0)
        discard;

    float solid_fraction = clamp(heatmap_solid_fraction, 0.0, 0.9);
    float half_fraction = solid_fraction + (1.0 - solid_fraction) * 0.4375;
    float half_alpha = 128.0 / 255.0;
    float radial_alpha = 1.0;
    if (distance_from_center > solid_fraction)
    {
        if (distance_from_center <= half_fraction)
        {
            float ratio = (distance_from_center - solid_fraction)
                / max(half_fraction - solid_fraction, 0.0001);
            radial_alpha = mix(1.0, half_alpha, ratio);
        }
        else
        {
            float ratio = (distance_from_center - half_fraction)
                / max(1.0 - half_fraction, 0.0001);
            radial_alpha = mix(half_alpha, 0.0, ratio);
        }
    }

    // The offscreen pass uses premultiplied source-over blending, matching
    // QPainter's ARGB32_Premultiplied accumulation before its final format
    // conversion. The next roadmap chunk validates how this is consumed.
    fragment_color = vec4(heatmap_color * radial_alpha, radial_alpha);
}
