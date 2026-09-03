#version 440

layout(binding = 1) uniform sampler2D icon_atlas;
layout(location = 0) in vec2 vertex_texture_coordinate;
layout(location = 1) in vec4 vertex_color;
layout(location = 0) out vec4 fragment_color;

void main()
{
    vec4 sampled = texture(icon_atlas, vertex_texture_coordinate);
    float alpha = vertex_color.a * sampled.a;
    if (alpha <= 0.001)
        discard;

    vec3 atlas_color = sampled.a > 0.001
        ? sampled.rgb / sampled.a
        : vec3(0.0);
    float maximum_channel = max(atlas_color.r, max(atlas_color.g, atlas_color.b));
    float minimum_channel = min(atlas_color.r, min(atlas_color.g, atlas_color.b));
    float fill_mask = smoothstep(0.08, 0.20, maximum_channel - minimum_channel);
    vec3 color = mix(atlas_color, vertex_color.rgb, fill_mask);
    fragment_color = vec4(color * alpha, alpha);
}
