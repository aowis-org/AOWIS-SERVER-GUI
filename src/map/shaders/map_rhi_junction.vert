#version 440

layout(location = 0) in vec3 sphere_position;
layout(location = 1) in vec3 sphere_normal;
layout(location = 2) in vec3 instance_center;
layout(location = 3) in float instance_radius;
layout(location = 4) in vec4 instance_color;

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

void main()
{
    vec3 world_position = instance_center + sphere_position * instance_radius;
    vec4 clip_position = camera.view_projection * vec4(world_position, 1.0);
    vec2 viewport = max(camera.viewport_and_sizes.xy, vec2(1.0));
    vec2 translation_ndc = camera.network_translation.xy * 2.0 / viewport;
    clip_position.xy += translation_ndc * clip_position.w;

    gl_Position = clip_position;
    vertex_normal = normalize(sphere_normal);
    vertex_color = instance_color;
}
