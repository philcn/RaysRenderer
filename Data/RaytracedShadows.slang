import Raytracing;
import Helpers;
#include "HostDeviceSharedMacros.h"

shared cbuffer PerFrameCB
{
    uint gFrameCount;
};

Texture2D gGBuf0;

RWTexture2D<float> gOutput;

struct ShadowRayData
{
    bool hit;
};

float3 SampleLightCone(float2 u, float3 N, float spread)
{
    float3 T = getPerpendicularStark(N);
    float3 B = normalize(cross(N, T));

    u.x *= spread;

    float r = sqrt(u.x);
    float phi = u.y * M_PI2;
    float3 L = float3(r * cos(phi), r * sin(phi), sqrt(max(0.0f, 1.0f - u.x)));

    return normalize(T * L.x + B * L.y + N * L.z);
}

[shader("raygeneration")]
void RayGen()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint2 launchDim = DispatchRaysDimensions().xy;

    uint randSeed = rand_init(launchIndex.x + launchIndex.y * launchDim.x, gFrameCount, 16);
    float2 randVal = float2(rand_next(randSeed), rand_next(randSeed));

    float3 posW = gGBuf0.Load(int3(launchIndex.xy, 0)).rgb;
    float3 direction;
    float maxT;

    LightData light = gLights[0];
    if (light.type == LightPoint)
    {
        direction = light.posW - posW;
        maxT = length(direction);
    }
    else if (light.type == LightDirectional)
    {
        direction = -light.dirW;
        maxT = 1000.0;
    }

    direction = SampleLightCone(randVal, direction, 0.02);

    RayDesc ray;
    ray.Origin = posW;
    ray.Direction = normalize(direction);
    ray.TMin = 0.01;
    ray.TMax = max(0.01, maxT);

    ShadowRayData payload;
    payload.hit = true;

    TraceRay(gRtScene, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, hitProgramCount, 0, ray, payload);
    gOutput[launchIndex.xy] = payload.hit ? 0.0f : 1.0f;
}

[shader("miss")]
void PrimaryMiss(inout ShadowRayData hitData)
{
    hitData.hit = false;
}

[shader("anyhit")]
void PrimaryAHS(inout ShadowRayData hitData, in BuiltInTriangleIntersectionAttributes attribs)
{
    hitData.hit = true;
}
