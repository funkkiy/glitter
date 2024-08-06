#version 460 core

in vec2 TexCoord;
in vec3 ObjectColor;

uniform sampler2D Texture;

out vec4 FragColor;

void main()
{
    FragColor = texture(Texture, TexCoord);
}
