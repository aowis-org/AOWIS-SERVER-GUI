#version 440

layout(location = 0) in vec3 vertex_normal;
layout(location = 1) in vec4 vertex_color;
layout(location = 2) in float vertex_selected;
layout(location = 0) out vec4 fragment_color;

void main()
{
    // Dither the buried orb in screen space. Color remains exactly the active
    // node symbology color; only coverage/opacity communicates underground.
    vec2 cell = floor(gl_FragCoord.xy / 3.0);
    if (mod(cell.x + cell.y, 2.0) > 0.5)
        discard;

    float alpha = vertex_color.a * 0.62;
    fragment_color = vec4(vertex_color.rgb * alpha, alpha);
}
