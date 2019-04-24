import Shading;
import GBufferUtils;

Texture2D gReflectionTexture;
Texture2D gShadowTexture;

cbuffer PerImageCB
{
    LightData gDirLight;
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

    return float4(color, 1.0);
}
