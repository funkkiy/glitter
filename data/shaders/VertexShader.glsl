#version 460 core

layout (location = 0) in vec3 PositionAttrib;

void main()
{
    gl_Position = vec4(PositionAttrib.x, PositionAttrib.y, PositionAttrib.z, 1.0);
}
