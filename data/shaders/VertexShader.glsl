#version 460 core

layout (location = 0) in vec3 PositionAttrib;
layout (location = 1) in vec2 TexCoordAttrib;

uniform vec3 uObjectColor;

layout (std140, binding = 0) uniform CommonData
{
    mat4 uView;
    mat4 uProjection;
};

layout (std140, binding = 1) uniform PerDrawData
{
    mat4 uModel;
};

out vec2 TexCoord;
out vec3 ObjectColor;

void main()
{
    gl_Position = uProjection * uView * uModel * vec4(PositionAttrib, 1.0);
    TexCoord = TexCoordAttrib;
    ObjectColor = uObjectColor;
}
