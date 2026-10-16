$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_globe_tile, 0);
// rgb: map background colour, w: background opacity ("slide opacity"), the
// same fade towards the background as the 2D imagery (fs_imagery_2d).
uniform vec4 u_globe_background;

void main()
{
    vec3 tile = texture2D(s_globe_tile, v_texcoord0).rgb;
    gl_FragColor = vec4(mix(tile, u_globe_background.rgb, u_globe_background.w), 1.0);
}
