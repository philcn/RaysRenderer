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
    float gPhiColor;
    float gPhiNormal;
    bool gEnableSpatialVarianceEstimation;
};

Texture2D gCompactNormDepth;
Texture2D gInputSignal;
Texture2D gMoments;
Texture2D gHistoryLength;

struct PsOut
{
    float4 signal : SV_TARGET0;
};

PsOut main(float2 texC : TEXCOORD, float4 pos : SV_POSITION)
{
    int2 ipos = int2(pos.xy);
    const int2 screenSize = GetTextureDims(gInputSignal, 0);

    float h = gHistoryLength[ipos].r;
    if (h >= 4.0 || !gEnableSpatialVarianceEstimation)
    {
        PsOut out;
        out.signal = gInputSignal[pos.xy];
        return out;
    }

    SVGFSample sampleCenter = FetchSignalSample(gInputSignal, gCompactNormDepth, ipos);
    if (sampleCenter.linearZ < 0) // not valid depth, must be skybox
    {
        PsOut out;
        out.signal = float4(sampleCenter.signal, sampleCenter.variance);
        return out;
    }

    const float phiColor = gPhiColor;
    const float phiDepth = max(sampleCenter.zDerivative, 1e-8) * 3.0;

    float sumWeight = 0.0;
    float3 sumSignal = 0.0;
    float2 sumMoments = 0.0;

    // Compute first and second moment spatially. This code also applies cross-bilateral filtering on the input color samples
    const int radius = 3;
    for (int yy = -radius; yy <= radius; ++yy)
    {
        for (int xx = -radius; xx <= radius; ++xx)
        {
            const int2 p = ipos + int2(xx, yy);
            const bool inside = all(greaterThanEqual(p, int2(0, 0))) && all(lessThan(p, screenSize));

            if (inside)
            {
                SVGFSample sampleP = FetchSignalSample(gInputSignal, gCompactNormDepth, p);
                const float2 momentsP = gMoments[p].rg;

                const float weight = ComputeWeight(sampleCenter, sampleP, phiDepth * length(float2(xx, yy)), gPhiNormal, phiColor);

                sumWeight += weight;
                sumSignal += sampleP.signal * weight;
                sumMoments += momentsP * weight;
            }
        }
    }

    sumWeight = max(sumWeight, 1e-6f);
    sumSignal /= sumWeight;
    sumMoments /= sumWeight;

    float variance = sumMoments.g - sumMoments.r * sumMoments.r;
    variance *= 4.0 / h; // Boost variance for first few frames

    PsOut out;
    out.signal = float4(sumSignal, variance);

    return out;
}
