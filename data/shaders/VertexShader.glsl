#version 460 core

layout (location = 0) in vec3 PositionAttrib;
layout (location = 1) in vec2 TexCoordAttrib;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uObjectColor;

out vec2 TexCoord;
out vec3 ObjectColor;

void main()
{
    gl_Position = uProjection * uView * uModel * vec4(PositionAttrib, 1.0);
    ObjectColor = uObjectColor;
}
