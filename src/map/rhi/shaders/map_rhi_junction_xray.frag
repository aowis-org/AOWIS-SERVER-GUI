#version 440

layout(location = 0) flat in vec4 vertex_color;
layout(location = 1) in vec2 sphere_coordinate;
layout(location = 2) in vec3 billboard_world_position;
layout(location = 3) flat in vec3 sphere_center;
layout(location = 4) flat in float sphere_radius;
layout(location = 5) flat in float vertex_selected;
layout(location = 0) out vec4 fragment_color;

void main()
{
    float radial_distance = length(sphere_coordinate);
    float edge_width = max(fwidth(radial_distance), 0.0001);
    float coverage = 1.0 - smoothstep(
        1.0 - edge_width, 1.0 + edge_width, radial_distance);

    // Dither the buried orb in screen space. Color remains exactly the active
    // node symbology color; only coverage/opacity communicates underground.
    vec2 cell = floor(gl_FragCoord.xy / 3.0);
    if (mod(cell.x + cell.y, 2.0) > 0.5)
        discard;

    float alpha = vertex_color.a * coverage * 0.62;
    if (alpha <= 0.001 || sphere_radius <= 0.0)
        discard;
    fragment_color = vec4(vertex_color.rgb * alpha, alpha);
}
