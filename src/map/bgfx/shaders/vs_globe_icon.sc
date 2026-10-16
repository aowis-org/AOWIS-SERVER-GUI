$input a_position, a_texcoord1, a_texcoord5, a_color0
$output v_color0, v_texcoord0

#include <bgfx_shader.sh>
#include "globe_network.sh"

// Globe pump/valve/tank/reservoir icon (MapNetworkIconVertex, six vertices
// per icon): a screen-aligned quad from the shared icon atlas. Pixel sizes
// are defined at the orbit distance and follow perspective; metre sizes are
// true world sizes.
void main()
{
    vec4 clip_position = mul(u_net_view_proj, vec4(a_position, 1.0));
    vec2 viewport = netViewport();
    float configured_size = u_net_params.y;
    float depth = max(abs(clip_position.w), 0.000001);
    float icon_size_px = max(configured_size, 0.0);
    if (configured_size < 0.0)
        icon_size_px = -configured_size * viewport.y / (2.0 * depth * NET_TAN_HALF_FOV);
    else
        icon_size_px *= max(u_net_params.x, 0.000001) / depth;
    vec2 offset_ndc = a_texcoord1 * icon_size_px * 2.0 / viewport;
    clip_position.xy += offset_ndc * clip_position.w;
    gl_Position = clip_position;
    v_texcoord0 = a_texcoord5;
    v_color0 = a_color0;
}
