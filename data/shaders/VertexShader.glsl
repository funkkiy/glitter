#version 460 core

layout (location = 0) in vec3 PositionAttrib;
layout (location = 1) in vec2 TexCoordAttrib;

uniform mat4 uModel;

void main()
{
    gl_Position = uModel * vec4(PositionAttrib, 1.0);
}
