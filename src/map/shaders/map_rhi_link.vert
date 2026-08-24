#version 440

layout(location = 0) in vec3 start_position;
layout(location = 1) in vec3 end_position;
layout(location = 2) in vec2 corner;
layout(location = 3) in vec4 color;
layout(location = 4) in float size_adjust_px;

layout(std140, binding = 0) uniform CameraBlock
{
    mat4 view_projection;
    vec4 viewport_and_sizes;
} camera;

layout(location = 0) out vec4 vertex_color;

void main()
{
    vec4 start_clip = camera.view_projection * vec4(start_position, 1.0);
    vec4 end_clip = camera.view_projection * vec4(end_position, 1.0);
    vec2 viewport = max(camera.viewport_and_sizes.xy, vec2(1.0));
    vec2 start_ndc = start_clip.xy / start_clip.w;
    vec2 end_ndc = end_clip.xy / end_clip.w;
    vec2 direction_pixels = (end_ndc - start_ndc) * viewport * 0.5;
    float direction_length = length(direction_pixels);
    vec2 normal = direction_length > 0.0001
        ? normalize(vec2(-direction_pixels.y, direction_pixels.x))
        : vec2(0.0, 1.0);

    vec4 clip_position = mix(start_clip, end_clip, corner.x);
    vec2 offset_ndc = normal * corner.y
        * (camera.viewport_and_sizes.z + size_adjust_px) * 2.0 / viewport;
    clip_position.xy += offset_ndc * clip_position.w;

    gl_Position = clip_position;
    vertex_color = color;
}
