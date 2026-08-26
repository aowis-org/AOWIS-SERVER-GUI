#version 440

layout(location = 0) in vec3 vertex_normal;
layout(location = 1) in vec4 vertex_color;
layout(location = 2) in float vertex_selected;
layout(location = 0) out vec4 fragment_color;

void main()
{
    vec3 normal = normalize(vertex_normal);

    // Camera-relative lighting makes the spherical volume visible regardless
    // of map heading. A broad key light, a softer fill light, edge darkening,
    // and a compact specular highlight make even very dark symbology colors
    // read as a real orb instead of a flat disc.
    vec3 key_direction = normalize(vec3(-0.42, -0.38, 0.82));
    vec3 fill_direction = normalize(vec3(0.58, 0.28, 0.58));
    vec3 view_direction = vec3(0.0, 0.0, 1.0);

    float key = max(dot(normal, key_direction), 0.0);
    float fill = max(dot(normal, fill_direction), 0.0);
    float facing = clamp(dot(normal, view_direction), 0.0, 1.0);

    float diffuse = 0.28 + key * 0.52 + fill * 0.16;
    float edge_shading = mix(0.55, 1.0, pow(facing, 0.38));

    vec3 base_color = clamp(vertex_color.rgb, vec3(0.0), vec3(1.0));
    float base_luminance = dot(base_color, vec3(0.2126, 0.7152, 0.0722));
    float dark_material = 1.0 - smoothstep(0.06, 0.32, base_luminance);

    // Pure black hydraulic symbology contains no diffuse information, so give
    // dark materials a small neutral reflectance. This preserves their color
    // while still exposing the curvature of the sphere.
    vec3 material_color = mix(
        base_color,
        vec3(0.16),
        dark_material * 0.52);

    vec3 half_vector = normalize(key_direction + view_direction);
    float specular = pow(max(dot(normal, half_vector), 0.0), 26.0) * 0.42;
    float broad_highlight = pow(max(dot(normal, key_direction), 0.0), 4.0) * 0.10;

    vec3 lit_color = material_color * diffuse * edge_shading;
    lit_color += vec3(specular + broad_highlight);

    float selected = clamp(vertex_selected, 0.0, 1.0);
    vec3 selection_color = vec3(0.0, 0.745, 1.0);
    vec3 selected_surface = selection_color * (0.82 + key * 0.16);
    selected_surface += vec3(0.04, 0.10, 0.14) * (0.55 + facing * 0.45);
    selected_surface += vec3(specular * 0.30);
    lit_color = mix(lit_color, selected_surface, selected);

    fragment_color = vec4(clamp(lit_color, vec3(0.0), vec3(1.0)), vertex_color.a);
}
