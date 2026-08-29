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

    // Positive W means a requested pixel radius. Keep that size stable at the
    // orbit focus depth while retaining normal perspective for junctions that
    // are nearer/farther than the focus. Negative W selects true world-space
    // sizing; the instance carries the already converted world-space radius.
    const float tan_half_fov = 0.4142135623730950;
    float configured_radius = camera.viewport_and_sizes.w;
    float radius_world = instance_radius;
    if (configured_radius >= 0.0)
    {
        float pixel_radius = configured_radius;
        float reference_depth = max(camera.network_translation.z, 0.000001);
        radius_world =
            2.0 * pixel_radius * reference_depth * tan_half_fov / viewport.y;
    }

    vec3 world_position = instance_center + sphere_position * radius_world;
    vec4 clip_position = camera.view_projection * vec4(world_position, 1.0);
    vec2 translation_ndc = camera.network_translation.xy * 2.0 / viewport;
    clip_position.xy += translation_ndc * clip_position.w;

    gl_Position = clip_position;
    vertex_normal = normalize(mat3(camera.view_projection) * sphere_normal);
    vertex_color = instance_color;
    vertex_selected = instance_selected;
}
