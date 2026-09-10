#version 440

layout(binding = 1) uniform sampler2D globe_tile;
layout(binding = 2) uniform sampler2D heatmap_tile;

layout(std140, binding = 0) uniform GlobeCameraBlock
{
    mat4 view_projection;
    vec4 heatmap_settings;
    vec4 basemap_settings;
} camera;

layout(location = 0) in vec2 vertex_texture_coordinate;
layout(location = 0) out vec4 fragment_color;

void main()
{
    vec3 tile_rgb = texture(globe_tile, vertex_texture_coordinate).rgb;
    vec3 mixed_rgb = mix(
        tile_rgb,
        camera.basemap_settings.rgb,
        clamp(camera.basemap_settings.a, 0.0, 1.0));
    // heatmap_tile is fully transparent (alpha 0) for every tile with no
    // heatmap texture of its own (see MapRhiGlobeRenderer::
    // rebuildTileBindings()'s heatmap_dummy_texture fallback) and for every
    // pixel outside a marker's radius within a tile that does have one (see
    // renderHeatmapTile()'s radial gradient), so this composition is already
    // a no-op almost everywhere without heatmap_settings.y needing to be
    // checked first -- but reading it directly (rather than, say, skipping
    // the sample when opacity is 0) means turning the heatmap on/off, or
    // adjusting its opacity, needs no tile texture regeneration at all,
    // exactly like MapRhiBasemapRenderer's identical split between "baked
    // into the texture" (color, radius, solid-fraction) and "read fresh
    // every frame" (opacity) -- see map_rhi_basemap.frag's matching blend.
    // Heatmap textures use premultiplied RGBA. This is algebraically the
    // same source-over result as mix(base, straight_rgb, alpha * opacity),
    // while allowing both QPainter and the GPU baker to keep their native
    // accumulation representation all the way into the sampled texture.
    vec4 heatmap_color = texture(heatmap_tile, vertex_texture_coordinate);
    float heatmap_opacity = clamp(camera.heatmap_settings.y, 0.0, 1.0);
    float heatmap_alpha = heatmap_color.a * heatmap_opacity;
    mixed_rgb = mixed_rgb * (1.0 - heatmap_alpha)
        + heatmap_color.rgb * heatmap_opacity;
    fragment_color = vec4(mixed_rgb, 1.0);
}
