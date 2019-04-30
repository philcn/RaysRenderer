import Raytracing;
import Shading;
import BRDF;
import Helpers;
import GBufferUtils;
#include "HostDeviceSharedMacros.h"

shared cbuffer PerFrameCB
{
    uint gFrameCount;
};

RWTexture2D<float4> gOutput;

struct ReflectionRayData
{
    float4 color;
    uint depth;
    float hitT;
    uint randSeed;
};

struct ShadowRayData
{
    bool hit;
};

[shader("raygeneration")]
void RayGen()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint2 launchDim = DispatchRaysDimensions().xy;

    uint randSeed = rand_init(launchIndex.x + launchIndex.y * launchDim.x, gFrameCount, 16);
    ShadingData sd = LoadGBuffer(launchIndex.xy);

    float3 reflectColor = TraceReflectionRay(sd, 0, randSeed);
    bool colorsNan = any(isnan(reflectColor));

    gOutput[launchIndex.xy] = float4(colorsNan ? float3(0.0) : reflectColor, 1.0);
}

bool TraceShadowRay(uint lightIndex, float3 origin)
{
    LightData light = gLights[lightIndex];
    float3 direction;
    float maxT;

    if (light.type == LightPoint)
    {
        direction = light.posW - origin;
        maxT = length(direction);
    }
    else if (light.type == LightDirectional)
    {
        direction = -light.dirW;
        maxT = 1000.0;
    }

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = normalize(direction);
    ray.TMin = 0.001;
    ray.TMax = max(0.01, maxT);

    ShadowRayData rayData;
    rayData.hit = true;
    TraceRay(gRtScene, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 1, hitProgramCount, 1, ray, rayData);
    return rayData.hit;
}

float3 TraceReflectionRay(ShadingData sd, uint rayDepth, uint randSeed)
{
    float2 randVal = float2(rand_next(randSeed), rand_next(randSeed));
    float3 H = getGGXMicrofacet(randVal, sd.N, sd.roughness);
    float3 L = reflect(-sd.V, H);

    RayDesc ray;
    ray.Origin = sd.posW;
    ray.Direction = L;
    ray.TMin = 0.001;
    ray.TMax = 100000;

    ReflectionRayData payload;
    payload.depth = rayDepth + 1;
    payload.randSeed = randSeed;

    TraceRay(gRtScene, 0, 0xFF, 0, hitProgramCount, 0, ray, payload);

    float NdotL = saturate(dot(sd.N, L));
    float NdotV = saturate(dot(sd.N, sd.V));
    float NdotH = saturate(dot(sd.N, H));
    float LdotH = saturate(dot(L, H));

    float D = evalGGX(sd.roughness, NdotH) * M_INV_PI; // Our GGX function does not include division by PI
    float G = evalSmithGGX(NdotL, NdotV, sd.roughness); // Includes division by 4 * NdotL * NdotV
    float3 F = fresnelSchlick(sd.specular, 1, max(0, LdotH));
    float3 brdf = D * G * F;
    float ggxProb = D * NdotH / (4 * LdotH);

    return payload.color.rgb * NdotL * brdf / ggxProb;
}

[shader("closesthit")]
void PrimaryCHS(inout ReflectionRayData hitData, in BuiltInTriangleIntersectionAttributes attribs)
{
    float3 rayOrigW = WorldRayOrigin();
    float3 rayDirW = WorldRayDirection(); 
    float hitT = RayTCurrent();
    float3 posW = rayOrigW + hitT * rayDirW;

    VertexOut v = getVertexAttributes(PrimitiveIndex(), attribs);
    ShadingData sd = prepareShadingData(v, gMaterial, rayOrigW, 0);

    float3 color = 0.0;

    [unroll]
    for (int i = 0; i < gLightsCount; i++)
    {
        if (TraceShadowRay(i, posW) == false)
        {
            color += evalMaterial(sd, gLights[i], 1.0).color.rgb;
        }
    }

    if (hitData.depth < 2) // perform 2nd bounce
    {
        color += TraceReflectionRay(sd, hitData.depth, hitData.randSeed);
    }

    hitData.hitT = hitT;
    hitData.color.rgb = color;
    hitData.color.a = 1;
}

[shader("miss")]
void PrimaryMiss(inout ReflectionRayData hitData)
{
    hitData.color = float4(0.2, 0.6, 0.9, 1.0);
    hitData.hitT = -1;
}

[shader("miss")]
void ShadowMiss(inout ShadowRayData hitData)
{
    hitData.hit = false;
}

[shader("anyhit")]
void ShadowAHS(inout ShadowRayData hitData, in BuiltInTriangleIntersectionAttributes attribs)
{
    hitData.hit = true;
}
