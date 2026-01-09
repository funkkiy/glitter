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

    // Spot Light.
    mat4 u_SpotLightView;
    mat4 u_SpotLightProjection;
    vec4 u_SpotLightPosition;
    vec4 u_SpotLightColor;
    vec4 u_SpotLightDirection;

    // Floats.
    float u_PointLightRadius;
    float u_SpotLightAngleCos;
    float u_SpotLightRange;
};

layout(std140, binding = 1) uniform PerDrawData
{
    mat4 u_Model;
    vec4 u_Opacity;
};

layout(binding = 0) uniform sampler2D u_Texture;
layout(binding = 1) uniform sampler2D u_SpotLightTexture;

out vec4 FragColor;

vec3 DiffuseSpecular(vec3 LightDir, vec3 Color, vec3 Normal)
{
    // Diffuse
    float NDotL = max(dot(Normal, LightDir), 0.0);
    vec3 Diffuse = NDotL * Color;

    // Specular
    float SpecularStrength = 0.5;
    vec3 ViewDir = normalize(u_EyePos.xyz - v_FragPos);
    vec3 HalfDir = normalize(ViewDir + LightDir);
    float Spec = pow(max(0.0, dot(HalfDir, Normal)), 32);
    vec3 Specular = vec3(SpecularStrength * Spec * Color);

    return Diffuse + Specular;
}

vec3 DirectionalLight(vec3 Direction, vec3 Color)
{
    vec3 Normal = normalize(v_Normal);
    vec3 LightDir = normalize(Direction);

    return DiffuseSpecular(LightDir, Color, Normal);
}

vec3 PointLight(vec3 Position, vec3 Color, float Radius)
{
    vec3 Normal = normalize(v_Normal);
    vec3 LightVec = Position - v_FragPos;
    vec3 LightDir = normalize(LightVec);

    // Distance-Based Attenuation
    float Distance = length(LightVec);
    float AttenuationTerm = (Distance / Radius) * 5.0;
    float Attenuation = 1.0 / (1.0 + (AttenuationTerm * AttenuationTerm));

    return Attenuation * DiffuseSpecular(LightDir, Color, Normal);
}

vec3 SpotLight(vec3 Position, vec3 Color, vec3 Direction, float Angle, float Range)
{
    vec3 Normal = normalize(v_Normal);
    vec3 LightVec = Position - v_FragPos;
    vec3 LightDir = normalize(LightVec);

    // Distance-Based Attenuation
    float Distance = length(LightVec);
    float AttenuationTerm = (Distance / Range) * 5.0;
    float Attenuation = 1.0 / (1.0 + (AttenuationTerm * AttenuationTerm));

    // Angular-Based Attenuation
    float CosDirection = dot(-LightDir, normalize(Direction));
    float AngularAttenuation = smoothstep(u_SpotLightAngleCos - 0.1, u_SpotLightAngleCos, CosDirection);

    return Attenuation * AngularAttenuation * DiffuseSpecular(LightDir, Color, Normal);
}

vec3 SpotLightGobo(vec3 Position, vec3 Color, vec3 Direction, float Angle, float Range)
{
    vec3 Normal = normalize(v_Normal);
    vec3 LightVec = Position - v_FragPos;
    vec3 LightDir = normalize(LightVec);

    // Distance-Based Attenuation
    float Distance = length(LightVec);
    float AttenuationTerm = (Distance / Range) * 5.0;
    float Attenuation = 1.0 / (1.0 + (AttenuationTerm * AttenuationTerm));

    // Angular-Based Attenuation
    float CosDirection = dot(-LightDir, normalize(Direction));
    float AngularAttenuation = smoothstep(u_SpotLightAngleCos - 0.1, u_SpotLightAngleCos, CosDirection);

    // Gobo Texture Projection
    vec4 FragSpotCoord = u_SpotLightProjection * u_SpotLightView * vec4(v_FragPos, 1.0);
    FragSpotCoord /= FragSpotCoord.w;
    FragSpotCoord.xy = (FragSpotCoord.xy * 0.5) + 0.5;
    vec3 Texture = texture(u_SpotLightTexture, FragSpotCoord.xy).rgb;

    return Attenuation * AngularAttenuation * Texture * DiffuseSpecular(LightDir, Color, Normal);
}

void main()
{
    vec3 EyePos = u_EyePos.xyz;

    // Ambient
    vec3 Ambient = vec3(0.1 * u_DirLightColor.rgb);

    vec3 CombinedDirectional = DirectionalLight(u_DirLightDirection.xyz, u_DirLightColor.rgb);
    vec3 CombinedPoint = PointLight(u_PointLightPosition.xyz, u_PointLightColor.rgb, u_PointLightRadius.x);
    vec3 CombinedSpot = SpotLightGobo(
        u_SpotLightPosition.xyz, u_SpotLightColor.rgb, u_SpotLightDirection.xyz, u_SpotLightAngleCos, u_SpotLightRange);

    // Result
    vec3 CombinedLight = Ambient + CombinedDirectional + CombinedPoint + CombinedSpot;
    FragColor = texture(u_Texture, v_TexCoord) * vec4(CombinedLight, u_Opacity.x);
}
