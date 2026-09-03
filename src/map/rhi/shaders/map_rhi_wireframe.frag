#version 440

layout(std140, binding = 0) uniform CameraBlock
{
    mat4 view_projection;
    vec4 viewport_and_sizes;
    vec4 heatmap_settings;
    vec4 basemap_settings;
    vec4 network_translation;
} camera;

layout(location = 0) out vec4 fragment_color;

void main()
{
    float luminance = dot(
        camera.basemap_settings.rgb,
        vec3(0.2126, 0.7152, 0.0722));
    vec3 wire_color = luminance > 0.5
        ? vec3(0.08)
        : vec3(0.92);
    fragment_color = vec4(wire_color, 1.0);
}
