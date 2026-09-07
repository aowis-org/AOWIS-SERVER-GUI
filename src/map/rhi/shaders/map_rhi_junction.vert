#version 440

layout(location = 0) in vec2 impostor_corner;
layout(location = 1) in vec3 instance_center;
layout(location = 2) in float instance_radius;
layout(location = 3) in vec4 instance_color;
layout(location = 4) in float instance_selected;

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

layout(location = 0) flat out vec4 vertex_color;
layout(location = 1) out vec2 sphere_coordinate;
layout(location = 2) out vec3 billboard_world_position;
layout(location = 3) flat out vec3 sphere_center;
layout(location = 4) flat out float sphere_radius;
layout(location = 5) flat out float vertex_selected;

void main()
{
    vec2 viewport = max(camera.viewport_and_sizes.xy, vec2(1.0));

    // Positive W means a requested pixel radius at the orbit focus depth.
    // Negative W selects true world-space sizing; the instance then carries
    // the already converted radius in the active scene's coordinate system.
    const float tan_half_fov = 0.4142135623730950;
    float radius_world = instance_radius;
    if (camera.viewport_and_sizes.w >= 0.0)
    {
        float reference_depth = max(camera.network_translation.z, 0.000001);
        radius_world = 2.0 * camera.viewport_and_sizes.w
            * reference_depth * tan_half_fov / viewport.y;
    }

    vec3 eye_to_center = instance_center - camera.camera_eye.xyz;
    float center_distance = max(length(eye_to_center), 0.000001);
    vec3 view_direction = eye_to_center / center_distance;
    float safe_radius = min(max(radius_world, 0.0), center_distance * 0.9999);

    // The quad lies on the sphere's tangent-cone cross section. This keeps
    // it just large enough to contain the analytic silhouette, including at
    // close camera distances, while the fragment shader rejects its corners.
    float tangent_denominator = sqrt(max(
        center_distance * center_distance - safe_radius * safe_radius,
        center_distance * center_distance * 0.000001));
    float tangent_radius = safe_radius * center_distance / tangent_denominator;

    // Reorient the camera axes onto the plane perpendicular to this sphere's
    // center ray. The common case is already orthonormal; Gram-Schmidt keeps
    // off-axis junctions conservative and stable in both flat 3D and Globe.
    vec3 billboard_right = camera.camera_right.xyz
        - view_direction * dot(camera.camera_right.xyz, view_direction);
    float right_length = length(billboard_right);
    if (right_length <= 0.000001)
        billboard_right = normalize(cross(camera.camera_up.xyz, view_direction));
    else
        billboard_right /= right_length;

    vec3 billboard_up = camera.camera_up.xyz
        - view_direction * dot(camera.camera_up.xyz, view_direction);
    billboard_up -= billboard_right * dot(billboard_up, billboard_right);
    float up_length = length(billboard_up);
    if (up_length <= 0.000001)
    {
        billboard_up = normalize(cross(view_direction, billboard_right));
        if (dot(billboard_up, camera.camera_up.xyz) < 0.0)
            billboard_up = -billboard_up;
    }
    else
    {
        billboard_up /= up_length;
    }

    vec3 world_position = instance_center + tangent_radius
        * (billboard_right * impostor_corner.x + billboard_up * impostor_corner.y);
    vec4 clip_position = camera.view_projection * vec4(world_position, 1.0);
    vec2 translation_ndc = camera.network_translation.xy * 2.0 / viewport;
    clip_position.xy += translation_ndc * clip_position.w;

    gl_Position = clip_position;
    vertex_color = instance_color;
    sphere_coordinate = impostor_corner;
    billboard_world_position = world_position;
    sphere_center = instance_center;
    sphere_radius = safe_radius;
    vertex_selected = instance_selected;
}
