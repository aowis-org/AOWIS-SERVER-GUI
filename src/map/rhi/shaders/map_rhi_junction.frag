#version 440

layout(std140, binding = 0) uniform CameraBlock
{
    mat4 view_projection;
    vec4 viewport_and_sizes;
    vec4 heatmap_settings;
    vec4 basemap_settings;
    vec4 network_translation;
    vec4 camera_right;
    vec4 camera_up;
    vec4 camera_eye;
    vec4 impostor_settings;
} camera;

layout(location = 0) flat in vec4 vertex_color;
layout(location = 1) in vec2 sphere_coordinate;
layout(location = 2) in vec3 billboard_world_position;
layout(location = 3) flat in vec3 sphere_center;
layout(location = 4) flat in float sphere_radius;
layout(location = 5) flat in vec4 vertex_state;
layout(location = 0) out vec4 fragment_color;

void main()
{
    if (vertex_state.b < 0.5)
        discard;

    float radial_distance = length(sphere_coordinate);
    float edge_width = max(fwidth(radial_distance), 0.0001);
    float coverage = 1.0 - smoothstep(
        1.0 - edge_width, 1.0 + edge_width, radial_distance);
    float alpha = vertex_color.a * coverage;
    if (alpha <= 0.001 || sphere_radius <= 0.0)
        discard;

    // Intersect the pixel ray with the real sphere and write that surface's
    // depth. The billboard therefore occludes exactly like the old mesh,
    // rather than behaving like a flat disc at the junction center.
    vec3 ray_vector = billboard_world_position - camera.camera_eye.xyz;
    float ray_length = length(ray_vector);
    if (ray_length <= 0.000001)
        discard;
    vec3 ray_direction = ray_vector / ray_length;
    vec3 eye_to_center = camera.camera_eye.xyz - sphere_center;
    float ray_projection = dot(eye_to_center, ray_direction);
    // Clamp the one-pixel antialias fringe to the tangent point instead of
    // discarding it; coverage still fades it to zero at the silhouette.
    float discriminant = max(
        ray_projection * ray_projection
            - (dot(eye_to_center, eye_to_center) - sphere_radius * sphere_radius),
        0.0);
    float root = sqrt(discriminant);
    float hit_distance = -ray_projection - root;
    if (hit_distance < 0.0)
        hit_distance = -ray_projection + root;
    if (hit_distance < 0.0)
        discard;

    vec3 surface_position = camera.camera_eye.xyz + ray_direction * hit_distance;
    vec4 surface_clip = camera.view_projection * vec4(surface_position, 1.0);
    float surface_ndc_depth = surface_clip.z / surface_clip.w;
    gl_FragDepth = clamp(
        surface_ndc_depth * camera.impostor_settings.x
            + camera.impostor_settings.y,
        0.0, 1.0);

    // Existing RHI network blending uses premultiplied color.
    fragment_color = vec4(vertex_color.rgb * alpha, alpha);
}
