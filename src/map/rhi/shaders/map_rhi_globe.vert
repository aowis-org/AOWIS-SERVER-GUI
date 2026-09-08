#version 440

layout(location = 0) in vec3 world_position;
layout(location = 1) in vec2 texture_coordinate;

layout(std140, binding = 0) uniform GlobeCameraBlock
{
    mat4 view_projection;
    // Only .y (opacity) is actually read (by map_rhi_globe.frag) -- kept
    // as a vec4, and named to match, purely for consistency with the
    // network/link/node CameraBlock's heatmap_settings elsewhere, not
    // because .x/.z/.w have any meaning here. See that fragment shader's
    // comment for why Globe's heatmap doesn't need a radius or
    // solid-fraction uniform at all: both are already baked into the
    // per-tile texture itself at generation time (see
    // MapRhiGlobeRenderer::renderHeatmapTile()), unlike the flat
    // network heatmap quads, which compute their radius per-vertex on
    // GPU.
    vec4 heatmap_settings;
    vec4 basemap_settings;
} camera;

layout(location = 0) out vec2 vertex_texture_coordinate;

void main()
{
    gl_Position = camera.view_projection * vec4(world_position, 1.0);
    vertex_texture_coordinate = texture_coordinate;
}
