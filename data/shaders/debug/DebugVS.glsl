#version 460 core

layout (location = 0) in vec3 a_Position;

layout (std140, binding = 0) uniform CommonData
{
    mat4 u_View;
    mat4 u_Projection;
    vec4 u_EyePos;
    vec4 u_LightPos;
    vec4 u_LightColor;
};

void main()
{
    gl_Position = u_Projection * u_View * vec4(a_Position, 1.0);
}
