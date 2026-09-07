#version 440

layout(location = 0) in vec3 center_position;
layout(location = 1) in vec3 east_direction;
layout(location = 2) in vec3 north_direction;
layout(location = 3) in vec2 corner;
layout(location = 4) in vec3 color;

layout(std140, binding = 0) uniform CameraBlock
{
    mat4 view_projection;
    vec4 viewport_and_sizes;
    vec4 heatmap_settings;
    vec4 basemap_settings;
    vec4 network_translation;
} camera;

layout(location = 0) out vec2 heatmap_corner;
layout(location = 1) out vec3 heatmap_color;

void main()
{
    vec2 viewport = max(camera.viewport_and_sizes.xy, vec2(1.0));

    // Same "requested pixel radius, held stable at the orbit focus depth"
    // convention map_rhi_junction.vert/map_rhi_node.vert use for
    // camera.viewport_and_sizes.w, applied here to heatmap_settings.x
    // instead (ThreeD's map_rhi_heatmap.vert has no equivalent of this --
    // it's handed an already-computed world-space radius in
    // heatmap_settings.w via a 2D Web Mercator zoom scale, which has no
    // meaning for Globe's real-meters ECEF space and perspective camera).
    const float tan_half_fov = 0.4142135623730950;
    float pixel_radius = camera.heatmap_settings.x;
    float reference_depth = max(camera.network_translation.z, 0.000001);
    float radius_world =
        2.0 * pixel_radius * reference_depth * tan_half_fov / viewport.y;

    // The actual Globe-specific step: expand the quad along this marker's
    // own local east/north tangent directions (computed once per marker on
    // the CPU, from its latitude/longitude -- see
    // MapRhiGlobeNetworkScene::appendHeatmap()) rather than a fixed world
    // X/Y plane. A flat ground plane is only a valid approximation of "lay
    // this decal on the ellipsoid surface, facing up" locally, at a single
    // point -- there is no single plane that works globally on a sphere,
    // unlike ThreeD's flat local tangent-plane world.
    vec3 world_position = center_position
        + (east_direction * corner.x + north_direction * corner.y) * radius_world;
    vec4 clip_position = camera.view_projection * vec4(world_position, 1.0);

    gl_Position = clip_position;
    heatmap_corner = corner;
    heatmap_color = color;
}
