#version 440

layout(location = 0) in vec3 sphere_position;
layout(location = 1) in vec3 sphere_normal;
layout(location = 2) in vec3 instance_center;
layout(location = 3) in float instance_radius;
layout(location = 4) in vec4 instance_color;
layout(location = 5) in float instance_selected;

layout(std140, binding = 0) uniform CameraBlock
{
    mat4 view_projection;
    vec4 viewport_and_sizes;
    vec4 heatmap_settings;
    vec4 basemap_settings;
    vec4 network_translation;
} camera;

layout(location = 0) out vec3 vertex_normal;
layout(location = 1) out vec4 vertex_color;
layout(location = 2) out float vertex_selected;

void main()
{
    vec2 viewport = max(camera.viewport_and_sizes.xy, vec2(1.0));

    // Junction size must not depend on the discrete map/tile zoom, otherwise
    // changing tile LOD makes every sphere jump in size. Use the continuous
    // orbit distance as the reference depth instead. A junction at the orbit
    // focus therefore keeps the familiar marker size, while normal perspective
    // naturally makes farther junctions smaller and nearer junctions larger.
    const float tan_half_fov = 0.4142135623730950;
    float pixel_radius = max(camera.viewport_and_sizes.w, 0.0);
    float reference_depth = max(camera.network_translation.z, 0.000001);
    float radius_world =
        2.0 * pixel_radius * reference_depth * tan_half_fov / viewport.y;

    vec3 world_position = instance_center + sphere_position * radius_world;
    vec4 clip_position = camera.view_projection * vec4(world_position, 1.0);
    vec2 translation_ndc = camera.network_translation.xy * 2.0 / viewport;
    clip_position.xy += translation_ndc * clip_position.w;

    gl_Position = clip_position;
    // Transform the sphere normal into camera-facing space for stable orb
    // shading. The sphere is isotropic, so normalizing after the transform is
    // sufficient here and keeps the highlight/rim visually tied to the view.
    vertex_normal = normalize(mat3(camera.view_projection) * sphere_normal);
    vertex_color = instance_color;
    vertex_selected = instance_selected;
}
