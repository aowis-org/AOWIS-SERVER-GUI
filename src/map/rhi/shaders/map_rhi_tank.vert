#version 440

layout(location = 0) in vec3 position_world;
layout(location = 1) in vec3 normal_world;
layout(location = 2) in vec2 texcoord;
layout(location = 3) in float selected;

layout(std140, binding = 0) uniform CameraBlock
{
    mat4 view_projection;
    vec4 viewport_and_sizes;
    vec4 heatmap_settings;
    vec4 basemap_settings;
    vec4 network_translation;
} camera;

layout(location = 0) out vec3 vertex_normal;
layout(location = 1) out vec2 vertex_texcoord;
layout(location = 2) out float vertex_height;
layout(location = 3) out float vertex_selected;

void main()
{
    vec4 clip_position = camera.view_projection * vec4(position_world, 1.0);
    vec2 viewport = max(camera.viewport_and_sizes.xy, vec2(1.0));
    vec2 translation_ndc = camera.network_translation.xy * 2.0 / viewport;
    clip_position.xy += translation_ndc * clip_position.w;

    gl_Position = clip_position;
    vertex_normal = normalize(normal_world);
    vertex_texcoord = texcoord;
    vertex_height = position_world.z;
    vertex_selected = selected;
}
