import Raytracing;
import Shading;
import BRDF;
import Helpers;
#include "HostDeviceSharedMacros.h"

shared cbuffer PerFrameCB
{
    float4x4 invView;
    float4x4 invModel;
    float2 viewportDims;
    float tanHalfFovY;
    uint gFrameCount;
};

[shader("raygeneration")]
void RayGen()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint2 launchDim = DispatchRaysDimensions().xy;

    float2 d = (((launchIndex.xy + 0.5) / viewportDims) * 2.f - 1.f);
    float aspectRatio = viewportDims.x / viewportDims.y;
    float3 viewDirection = normalize((d.x * invView[0].xyz * tanHalfFovY * aspectRatio) - (d.y * invView[1].xyz * tanHalfFovY) - invView[2].xyz);
}
