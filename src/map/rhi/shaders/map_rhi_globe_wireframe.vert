#version 440

layout(location = 0) in vec3 world_position;

layout(std140, binding = 0) uniform GlobeCameraBlock
{
    mat4 view_projection;
} camera;

void main()
{
    gl_Position = camera.view_projection * vec4(world_position, 1.0);
}
