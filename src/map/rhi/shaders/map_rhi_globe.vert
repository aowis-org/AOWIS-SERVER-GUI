#version 440

layout(location = 0) in vec3 world_position;
layout(location = 1) in vec2 texture_coordinate;

layout(std140, binding = 0) uniform GlobeCameraBlock
{
    mat4 view_projection;
} camera;

layout(location = 0) out vec2 vertex_texture_coordinate;

void main()
{
    gl_Position = camera.view_projection * vec4(world_position, 1.0);
    vertex_texture_coordinate = texture_coordinate;
}
