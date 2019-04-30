/**********************************************************************************************************************
# Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
# following conditions are met:
#  * Redistributions of code must retain the copyright notice, this list of conditions and the following disclaimer.
#  * Neither the name of NVIDIA CORPORATION nor the names of its contributors may be used to endorse or promote products
#    derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT
# SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
# OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********************************************************************************************************************/

#include "SVGFUtils.h"

cbuffer PerPassCB
{
    float gAlpha;
    float gMomentsAlpha;
};

Texture2D gInputSignal;
Texture2D gLinearZ;
Texture2D gMotion;
Texture2D gPrevLinearZ;
Texture2D gPrevInputSignal;
Texture2D gPrevMoments;
Texture2D gHistoryLength;

struct PsOut
{
    float4 signal : SV_TARGET0;
    float2 moments : SV_TARGET1;
    float historyLength : SV_TARGET2;
};

bool IsReprjValid(int2 coord, float Z, float Zprev, float zDeriv, float3 normal, float3 normalPrev, float normalDeriv)
{
    const int2 imageDim = GetTextureDims(gInputSignal, 0);

    // check whether reprojected pixel is inside of the screen
    if (any(lessThan(coord, int2(1, 1))) || any(greaterThan(coord, imageDim - int2(1, 1)))) return false;

    // check if deviation of depths is acceptable
    if (abs(Zprev - Z) / (zDeriv + 1e-4) > 2.0) return false;

    // check normals for compatibility
    if (distance(normal, normalPrev) / (normalDeriv + 1e-2) > 16.0) return false;

    return true;
}

bool ReprojectLastFilteredData(float2 fragCoord, out float3 prevSignal, out float2 prevMoments, out float historyLength)
{
    const int2 ipos = fragCoord;
    const float2 imageDim = float2(GetTextureDims(gInputSignal, 0));

    // .xy motion, .z position derivative (unused), .w normal derivative
    const float4 motion = gMotion[ipos];

    // .x Z, .y Z derivative, .z last frame Z, .w world normal
    const float4 depth = gLinearZ[ipos];
    const float3 normal = OctToDir(asuint(depth.w));

    const int2 iposPrev = int2(float2(ipos) + motion.xy * imageDim + float2(0.5, 0.5));
    const float2 posPrev = floor(fragCoord.xy) + motion.xy * imageDim;

    prevSignal = 0.0;
    prevMoments = 0.0;
    historyLength = gHistoryLength[iposPrev].r;

    bool v[4];
    const int2 offset[4] = { int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1) };

    // Check for all 4 taps of the bilinear filter for validity
    bool valid = false;
    for (int sampleIdx = 0; sampleIdx < 4; ++sampleIdx)
    {
        const int2 loc = int2(posPrev) + offset[sampleIdx];
        const float4 depthPrev = gPrevLinearZ[loc];
        const float3 normalPrev = OctToDir(asuint(depthPrev.w));

        v[sampleIdx] = IsReprjValid(iposPrev, depth.z, depthPrev.x, depth.y, normal, normalPrev, motion.w);

        valid = valid || v[sampleIdx];
    }

    // Perform bilinear interpolation
    if (valid)
    {
        float sumWeights = 0.0;
        const float x = frac(posPrev.x);
        const float y = frac(posPrev.y);

        const float w[4] = { (1 - x) * (1 - y),
                                  x  * (1 - y),
                             (1 - x) *      y,
                                  x  *      y };

        for (int sampleIdx = 0; sampleIdx < 4; ++sampleIdx)
        {
            if (v[sampleIdx])
            {
                const int2 loc = int2(posPrev) + offset[sampleIdx];
                prevSignal  += w[sampleIdx] * gPrevInputSignal[loc].rgb;
                prevMoments += w[sampleIdx] * gPrevMoments[loc].rg;
                sumWeights  += w[sampleIdx];
            }
        }

        // Redistribute weights in case not all taps were used
        valid = (sumWeights >= 0.01);
        prevSignal = valid ? prevSignal / sumWeights : 0.0;
        prevMoments = valid ? prevMoments / sumWeights : 0.0;
    }

    // Perform a cross-bilateral filter with binary decision to find some suitable samples spatially
    if (!valid)
    {
        float cnt = 0.0;
        const int radius = 1;
        for (int yy = -radius; yy <= radius; ++yy)
        {
            for (int xx = -radius; xx <= radius; ++xx)
            {
                int2 p = iposPrev + int2(xx, yy);
                float4 depthP = gPrevLinearZ[p];
                float3 normalP = OctToDir(asuint(depthP.w));

                if (IsReprjValid(iposPrev, depth.z, depthP.x, depth.y, normal, normalP, motion.w))
                {
                    prevSignal += gPrevInputSignal[p].rgb;
                    prevMoments += gPrevMoments[p].rg;
                    cnt += 1.0;
                }
            }
        }

        if (cnt > 0.0)
        {
            valid = true;
            prevSignal /= cnt;
            prevMoments /= cnt;
        }
    }

    if (!valid)
    {
        prevSignal = 0.0;
        prevMoments = 0.0;
        historyLength = 0.0;
    }

    return valid;
}

PsOut main(float2 texC : TEXCOORD, float4 pos : SV_POSITION)
{
    const float3 signal = gInputSignal[pos.xy].rgb;

    float historyLength;
    float3 prevSignal;
    float2 prevMoments;
    bool success = ReprojectLastFilteredData(pos.xy, prevSignal, prevMoments, historyLength);

    historyLength = min(32.0f, success ? historyLength + 1.0f : 1.0f);

    // This adjusts the alpha for the case where insufficient history is available.
    // It boosts the temporal accumulation to give the samples equal weights in the beginning.
    const float alpha = success ? max(gAlpha, 1.0 / historyLength) : 1.0;
    const float alphaMoments = success ? max(gMomentsAlpha, 1.0 / historyLength) : 1.0;

    float2 moments;
    moments.r = luminance(signal);
    moments.g = moments.r * moments.r;
    moments = lerp(prevMoments, moments, alphaMoments);

    PsOut out;
    out.signal.rgb = lerp(prevSignal, signal, alpha);
    out.signal.a = max(0.0, moments.g - moments.r * moments.r);
    out.moments = moments;
    out.historyLength = historyLength;

    return out;
}
