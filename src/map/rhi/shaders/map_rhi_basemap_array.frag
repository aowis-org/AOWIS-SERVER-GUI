#version 440

layout(binding = 1) uniform sampler2DArray basemap_tiles;

layout(std140, binding = 0) uniform CameraBlock
{
    mat4 view_projection;
    vec4 viewport_and_sizes;
    vec4 heatmap_settings;
    vec4 basemap_settings;
    vec4 network_translation;
} camera;

layout(location = 0) in vec2 vertex_texture_coordinate;
layout(location = 1) flat in float vertex_texture_layer;
layout(location = 0) out vec4 fragment_color;

void main()
{
    if (vertex_texture_layer < 0.5)
        discard;

    vec4 tile_color = texture(
        basemap_tiles,
        vec3(vertex_texture_coordinate, vertex_texture_layer));
    vec3 mixed_rgb = mix(
        tile_color.rgb,
        camera.basemap_settings.rgb,
        clamp(camera.basemap_settings.a, 0.0, 1.0));
    fragment_color = vec4(mixed_rgb, 1.0);
}
