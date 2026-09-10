#version 440

layout(binding = 1) uniform sampler2DArray globe_tiles;
layout(binding = 2) uniform sampler2DArray globe_heatmap_tiles;

layout(std140, binding = 0) uniform GlobeCameraBlock
{
    mat4 view_projection;
    vec4 heatmap_settings;
    vec4 basemap_settings;
} camera;

layout(location = 0) in vec2 vertex_texture_coordinate;
layout(location = 1) flat in float vertex_imagery_texture_layer;
layout(location = 2) flat in float vertex_heatmap_texture_layer;
layout(location = 0) out vec4 fragment_color;

void main()
{
    if (vertex_imagery_texture_layer < 0.5)
        discard;

    vec3 mixed_rgb = texture(
        globe_tiles,
        vec3(vertex_texture_coordinate, vertex_imagery_texture_layer)).rgb;
    mixed_rgb = mix(
        mixed_rgb,
        camera.basemap_settings.rgb,
        clamp(camera.basemap_settings.a, 0.0, 1.0));

    if (vertex_heatmap_texture_layer >= 0.5
        && camera.heatmap_settings.y > 0.0)
    {
        // Heatmap array layers use the same premultiplied representation as
        // the per-tile fallback textures.
        vec4 heatmap_color = texture(
            globe_heatmap_tiles,
            vec3(vertex_texture_coordinate, vertex_heatmap_texture_layer));
        float heatmap_opacity = clamp(
            camera.heatmap_settings.y, 0.0, 1.0);
        float heatmap_alpha = heatmap_color.a * heatmap_opacity;
        mixed_rgb = mixed_rgb * (1.0 - heatmap_alpha)
            + heatmap_color.rgb * heatmap_opacity;
    }
    fragment_color = vec4(mixed_rgb, 1.0);
}
