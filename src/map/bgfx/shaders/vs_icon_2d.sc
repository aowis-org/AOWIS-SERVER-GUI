$input a_position, a_texcoord0, a_texcoord1, a_color0
$output v_color0, v_texcoord0

#include <bgfx_shader.sh>

// Chunk-local world -> clip: xy = chunk origin in clip, zw = clip per world unit.
uniform vec4 u_network_camera;
// xy = clip per screen pixel (x right, y down).
uniform vec4 u_network_pixel;
// x = icon size in pixels (symbology icon size, px or metres converted per frame).
uniform vec4 u_icon_style;

// Icons: a_texcoord0 = corner offset in units of the icon size (the atlas
// aspect ratio is baked in), a_texcoord1 = atlas coordinates.
void main()
{
    vec2 clip = u_network_camera.xy + a_position.xy * u_network_camera.zw;
    gl_Position = vec4(clip + a_texcoord0.xy * u_icon_style.x * u_network_pixel.xy, 0.0, 1.0);
    v_color0 = a_color0;
    v_texcoord0 = a_texcoord1;
}
