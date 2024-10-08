#version 460 core

in vec2 v_TexCoord;
in vec3 v_Normal;
in vec3 v_FragPos;

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

uniform sampler2D u_Texture;

out vec4 FragColor;

void main()
{ 
    vec3 EyePos = u_EyePos.xyz;
    vec3 LightPos = u_LightPos.xyz;
    vec3 LightColor = u_LightColor.rgb;

    // Ambient
    vec3 Ambient = vec3(0.1 * LightColor);

    // Diffuse
    vec3 Normal = normalize(v_Normal);
    vec3 LightDir = normalize(LightPos - v_FragPos);
    float NDotL = max(dot(Normal, LightDir), 0.0);
    vec3 Diffuse = NDotL * LightColor;

    // Specular
    float SpecularStrength = 0.5;
    vec3 ViewDir = normalize(EyePos - v_FragPos);
    vec3 HalfDir = normalize(ViewDir + LightDir);
    float Spec = pow(max(0.0, dot(HalfDir, Normal)), 32);
    vec3 Specular = vec3(SpecularStrength * Spec * LightColor);

    // Result
    vec3 CombinedLight = Ambient + Diffuse + Specular;
    FragColor = texture(u_Texture, v_TexCoord) * vec4(CombinedLight, u_Opacity);
}
