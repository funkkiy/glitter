#version 460 core

layout (location = 0) in vec3 a_Position;
layout (location = 1) in vec2 a_TexCoord;
layout (location = 2) in vec3 a_Normal;

layout (std140, binding = 0) uniform CommonData
{
    mat4 u_View;
    mat4 u_Projection;
    vec4 u_EyePos;
    vec4 u_LightPos;
    vec4 u_LightColor;
};

layout (std140, binding = 1) uniform PerDrawData
{
    mat4 u_Model;
    float u_Opacity;
};

out vec2 v_TexCoord;
out vec3 v_Normal;
out vec3 v_FragPos;
out vec4 v_EyePos;

void main()
{
    gl_Position = u_Projection * u_View * u_Model * vec4(a_Position, 1.0);

    v_TexCoord = a_TexCoord;
    v_Normal = a_Normal;
    v_FragPos = vec3(u_Model * vec4(a_Position, 1.0));
    v_EyePos = u_EyePos;
}
