#version 460 core

in vec2 v_TexCoord;
in vec3 v_Normal;
in vec3 v_FragPos;
in vec4 v_EyePos;

uniform sampler2D u_Texture;

out vec4 FragColor;

void main()
{
    vec3 LightColor = vec3(1.0, 1.0, 1.0);

    // Ambient
    vec3 Ambient = vec3(0.1 * LightColor);

    // Diffuse
    vec3 Normal = normalize(v_Normal);
    vec3 LightDir = normalize(vec3(1.0, 0.5, -0.5));
    float NDotL = max(0.0, dot(Normal, LightDir));
    vec3 Diffuse = NDotL * LightColor;

    // Specular
    float SpecularStrength = 0.5;
    vec3 ViewDir = normalize(v_EyePos.xyz - v_FragPos);
    vec3 HalfDir = normalize(ViewDir + LightDir);
    float Spec = pow(max(0.0, dot(HalfDir, Normal)), 32);
    vec3 Specular = vec3(SpecularStrength * Spec * LightColor);

    // Result
    vec3 CombinedLight = Ambient + Diffuse + Specular;
    FragColor = texture(u_Texture, v_TexCoord) * vec4(CombinedLight, 1.0);
}
