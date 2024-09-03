#version 460 core

in vec2 v_TexCoord;

uniform layout(location = 0) sampler2D u_ColorTexture;
uniform layout(location = 1) float u_Gamma;

out vec4 FragColor;

void main()
{ 
    FragColor = texture(u_ColorTexture, v_TexCoord) * u_Gamma;
}
