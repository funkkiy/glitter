#version 460 core

in vec2 v_TexCoord;
in vec3 v_Normal;
in vec3 v_FragPos;

layout(std140, binding = 0) uniform CommonData
{
    mat4 u_View;
    mat4 u_Projection;
    vec4 u_EyePos;

    // Directional Light.
    vec4 u_DirLightDirection;
    vec4 u_DirLightColor;

    // Point Light.
    vec4 u_PointLightPosition;
    vec4 u_PointLightColor;
    vec4 u_PointLightAttenuation;
    float u_PointLightRadius;
};

layout(std140, binding = 1) uniform PerDrawData
{
    mat4 u_Model;
    vec4 u_Opacity;
};

uniform sampler2D u_Texture;

out vec4 FragColor;

vec3 DirectionalLight(vec3 Direction, vec3 Color)
{
    vec3 Normal = normalize(v_Normal);
    vec3 LightDir = normalize(Direction);

    // Diffuse
    float NDotL = max(dot(Normal, LightDir), 0.0);
    vec3 Diffuse = NDotL * Color;

    // Specular
    float SpecularStrength = 0.5;
    vec3 ViewDir = normalize(u_EyePos.xyz);
    vec3 HalfDir = normalize(ViewDir + LightDir);
    float Spec = pow(max(0.0, dot(HalfDir, Normal)), 32);
    vec3 Specular = vec3(SpecularStrength * Spec * Color);

    return Diffuse + Specular;
}

vec3 PointLight(vec3 Position, vec3 Color, float Radius)
{
    vec3 Normal = normalize(v_Normal);
    vec3 LightDir = normalize(Position - v_FragPos);

    // Attenuation
    float Distance = length(LightDir);
    float Attenuation = 1.0 / (u_PointLightAttenuation.x + u_PointLightAttenuation.y * Distance + u_PointLightAttenuation.z * (Distance * Distance));

    // Diffuse
    float NDotL = max(dot(Normal, LightDir), 0.0);
    vec3 Diffuse = NDotL * Color;

    // Specular
    float SpecularStrength = 0.5;
    vec3 ViewDir = normalize(u_EyePos.xyz - v_FragPos);
    vec3 HalfDir = normalize(ViewDir + LightDir);
    float Spec = pow(max(0.0, dot(HalfDir, Normal)), 32);
    vec3 Specular = vec3(SpecularStrength * Spec * Color);

    return Attenuation * (Diffuse + Specular);
}

void main()
{
    vec3 EyePos = u_EyePos.xyz;

    // Ambient
    vec3 Ambient = vec3(0.1 * u_DirLightColor.rgb);

    vec3 CombinedDirectional = DirectionalLight(u_DirLightDirection.xyz, u_DirLightColor.rgb);
    vec3 CombinedPoint = PointLight(u_PointLightPosition.xyz, u_PointLightColor.rgb, u_PointLightRadius.x);

    // Result
    vec3 CombinedLight = Ambient + CombinedDirectional + CombinedPoint;
    FragColor = texture(u_Texture, v_TexCoord) * vec4(CombinedLight, u_Opacity.x);
}
