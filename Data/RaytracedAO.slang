import Raytracing;
import Helpers;
#include "HostDeviceSharedMacros.h"

shared cbuffer PerFrameCB
{
    uint gFrameCount;
    float gAODistance;
};

shared Texture2D gGBuf0;
shared Texture2D gGBuf1;

shared RWTexture2D<float> gOutput;

struct AORayData
{
    bool hit;
};

[shader("raygeneration")]
void RayGen()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint2 launchDim = DispatchRaysDimensions().xy;

    float3 posW = gGBuf0.Load(int3(launchIndex.xy, 0)).rgb;
    float3 normalW = gGBuf1.Load(int3(launchIndex.xy, 0)).rgb;

    uint randSeed = rand_init(launchIndex.x + launchIndex.y * launchDim.x, gFrameCount, 16);
    float2 randVal = float2(rand_next(randSeed), rand_next(randSeed));

    float3 direction = getCosHemisphereSample(randVal, normalW, getPerpendicularStark(normalW));

    RayDesc ray;
    ray.Origin = posW;
    ray.Direction = normalize(direction);
    ray.TMin = 0.01;
    ray.TMax = max(0.01, gAODistance);

    AORayData payload;
    payload.hit = true;

    TraceRay(gRtScene, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, hitProgramCount, 0, ray, payload);
    gOutput[launchIndex.xy] = payload.hit ? 0.0f : 1.0f;
}

[shader("miss")]
void PrimaryMiss(inout AORayData hitData)
{
    hitData.hit = false;
}

[shader("anyhit")]
void PrimaryAHS(inout AORayData hitData, in BuiltInTriangleIntersectionAttributes attribs)
{
    hitData.hit = true;
}
