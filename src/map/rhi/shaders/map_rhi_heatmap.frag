#version 440

layout(std140, binding = 0) uniform CameraBlock
{
    mat4 view_projection;
    vec4 viewport_and_sizes;
    vec4 heatmap_settings;
    vec4 basemap_settings;
    vec4 network_translation;
} camera;

layout(location = 0) in vec2 heatmap_corner;
layout(location = 1) in vec3 heatmap_color;
layout(location = 0) out vec4 fragment_color;

void main()
{
    float distance_from_center = length(heatmap_corner);
    if (distance_from_center >= 1.0)
        discard;

    float solid_fraction = clamp(camera.heatmap_settings.z, 0.0, 0.9);
    float half_fraction = solid_fraction + (1.0 - solid_fraction) * 0.4375;
    float radial_alpha = 1.0;
    if (distance_from_center > solid_fraction)
    {
        if (distance_from_center <= half_fraction)
        {
            float ratio = (distance_from_center - solid_fraction)
                / max(half_fraction - solid_fraction, 0.0001);
            radial_alpha = mix(1.0, 0.5, ratio);
        }
        else
        {
            float ratio = (distance_from_center - half_fraction)
                / max(1.0 - half_fraction, 0.0001);
            radial_alpha = mix(0.5, 0.0, ratio);
        }
    }

    float alpha = radial_alpha * clamp(camera.heatmap_settings.y, 0.0, 1.0);
    if (alpha <= 0.001)
        discard;

    fragment_color = vec4(heatmap_color * alpha, alpha);
}
