$input a_position, a_texcoord0, a_color0
$output v_color0, v_texcoord0

#include <bgfx_shader.sh>

// Chunk-local world -> clip: xy = chunk origin in clip, zw = clip per world unit.
uniform vec4 u_network_camera;
// xy = clip per screen pixel (x right, y down).
uniform vec4 u_network_pixel;
// x = marker half size in pixels.
uniform vec4 u_marker_style;

// Markers: a_texcoord0 holds unit corners (-1/+1); the size is a uniform so a
// symbology size change never rebuilds geometry. The corner is passed on so
// the fragment shader can cut an antialiased circle.
void main()
{
    vec2 clip = u_network_camera.xy + a_position.xy * u_network_camera.zw;
    gl_Position = vec4(clip + a_texcoord0.xy * u_marker_style.x * u_network_pixel.xy, 0.0, 1.0);
    v_color0 = a_color0;
    v_texcoord0 = a_texcoord0;
}
