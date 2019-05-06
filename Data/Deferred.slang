import Shading;
import GBufferUtils;

Texture2D gReflectionTexture;
Texture2D gShadowTexture;
Texture2D gAOTexture;

cbuffer PerImageCB
{
    LightData gDirLight;
    float gNearFieldGIStrength;
};

float4 main(float2 texC : TEXCOORD, float4 pos : SV_POSITION) : SV_TARGET
{
    ShadingData sd = LoadGBuffer(pos.xy);
    if (sd.opacity <= 0)
    {
        discard;
    }

    float3 color = 0.0;
    float shadowFactor = 1.0;

#if defined(RAYTRACE_SHADOWS)
    shadowFactor = gShadowTexture.Load(int3(pos.xy, 0)).r;
#endif

    color = evalMaterial(sd, gLights[0], shadowFactor).color.rgb;

#if defined(RAYTRACE_REFLECTIONS)
    color += gReflectionTexture.Load(int3(pos.xy, 0)).rgb;
#endif

#if defined(RAYTRACE_AO)
    const float ao = gAOTexture.Load(int3(pos.xy, 0)).r;
#if defined(NEAR_FIELD_GI_APPROX)
    // Polynomial approximation from "Practical Realtime Strategies for Accurate Indirect Occlusion"
    const float3 albedo = sd.diffuse;
    const float3 A = 2.0404 * albedo - 0.3324;
    const float3 B = 4.7951 * albedo - 0.6417;
    const float3 C = 2.7552 * albedo + 0.6903;
    const float ao2 = ao * ao;
    const float ao3 = ao2 * ao;
    const float3 gi = A * ao3 - B * ao2 + C * ao;
    color *= lerp(ao.rrr, gi, gNearFieldGIStrength);
#else
    color *= ao;
#endif
#endif

    return float4(color, 1.0);
}
