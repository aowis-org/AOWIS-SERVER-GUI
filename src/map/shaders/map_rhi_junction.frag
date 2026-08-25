#version 440

layout(location = 0) in vec3 vertex_normal;
layout(location = 1) in vec4 vertex_color;
layout(location = 0) out vec4 fragment_color;

void main()
{
    vec3 normal = normalize(vertex_normal);
    vec3 key_direction = normalize(vec3(-0.40, -0.32, 0.86));
    vec3 fill_direction = normalize(vec3(0.55, 0.42, 0.48));

    float key = max(dot(normal, key_direction), 0.0);
    float fill = max(dot(normal, fill_direction), 0.0);
    float sky = clamp(normal.z * 0.5 + 0.5, 0.0, 1.0);
    float lighting = 0.42 + key * 0.43 + fill * 0.12 + sky * 0.10;

    vec3 half_vector = normalize(key_direction + vec3(0.0, 0.0, 1.0));
    float specular = pow(max(dot(normal, half_vector), 0.0), 22.0) * 0.20;
    vec3 lit = vertex_color.rgb * lighting + vec3(specular);
    fragment_color = vec4(lit, vertex_color.a);
}
