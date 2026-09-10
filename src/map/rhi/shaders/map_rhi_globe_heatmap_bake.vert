#version 440

layout(location = 0) in vec2 stamp_corner;
layout(location = 1) in vec2 stamp_center_pixels;
layout(location = 2) in vec2 stamp_radius_and_solid_fraction;
layout(location = 3) in vec3 stamp_color;
layout(location = 4) in vec2 stamp_target_scale_and_offset_x;

layout(location = 0) out vec2 heatmap_corner;
layout(location = 1) flat out float heatmap_solid_fraction;
layout(location = 2) flat out vec3 heatmap_color;

const float HeatmapTextureSize = 256.0;

void main()
{
    float radius_pixels = max(
        stamp_radius_and_solid_fraction.x, 0.0001);
    vec2 position_pixels = clamp(
        stamp_center_pixels + stamp_corner * radius_pixels,
        vec2(0.0), vec2(HeatmapTextureSize));
    vec2 normalized_position = position_pixels / HeatmapTextureSize;
    normalized_position.x =
        normalized_position.x * stamp_target_scale_and_offset_x.x
        + stamp_target_scale_and_offset_x.y;
    gl_Position = vec4(
        normalized_position.x * 2.0 - 1.0,
        1.0 - normalized_position.y * 2.0,
        0.0,
        1.0);
    // This is equivalent to clipping the original quad at a standalone
    // 256x256 render target, while preventing it from entering a neighbor's
    // atlas slot. Recomputing the corner coordinate preserves the gradient
    // at every newly-clamped edge.
    heatmap_corner =
        (position_pixels - stamp_center_pixels) / radius_pixels;
    heatmap_solid_fraction = stamp_radius_and_solid_fraction.y;
    heatmap_color = stamp_color;
}
