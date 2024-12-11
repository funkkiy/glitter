#version 460 core

out vec2 v_TexCoord;

void main()
{
    vec2 PpfxTriangle[3] = {
        vec2(-1.0, -1.0),
        vec2(3.0, -1.0),
        vec2(-1.0, 3.0)
    };

    vec2 Position = PpfxTriangle[gl_VertexID];
    gl_Position = vec4(Position, 0.0, 1.0);
    v_TexCoord = (Position * 0.5) + 0.5;
}
