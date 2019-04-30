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
#include "HostDeviceSharedCode.h"

cbuffer PerPassCB
{
    uint gStepSize;
    float gPhiColor;
    float gPhiNormal;
};

Texture2D gCompactNormDepth;
Texture2D gInputSignal;

struct PsOut
{
    float4 signal : SV_TARGET0;
};

float ComputeVarianceCenter(int2 ipos, Texture2D signalTexture)
{
    const float kernel[2][2] = {
        { 1.0 / 4.0, 1.0 / 8.0 },
        { 1.0 / 8.0, 1.0 / 16.0 }
    };

    float sum = 0.0;
    const int radius = 1;
    for (int yy = -radius; yy <= radius; yy++)
    {
        for (int xx = -radius; xx <= radius; xx++)
        {
            int2 p = ipos + int2(xx, yy);
            float k = kernel[abs(xx)][abs(yy)];
            sum += signalTexture.Load(int3(p, 0)).a * k;
        }
    }

    return sum;
}

PsOut main(float2 texC : TEXCOORD, float4 pos : SV_POSITION)
{
    const int2 ipos = int2(pos.xy);
    const int2 screenSize = getTextureDims(gInputSignal, 0);

    SVGFSample sampleCenter = FetchSignalSample(gInputSignal, gCompactNormDepth, ipos);

    PsOut out;
    if (sampleCenter.linearZ < 0) // not valid depth, must be skybox
    {
        out.signal = float4(sampleCenter.signal, sampleCenter.variance);
        return out;
    }

    const float epsVariance = 1e-10;
    const float kernelWeights[3] = { 1.0, 2.0 / 3.0, 1.0 / 6.0 };
    const float variance = ComputeVarianceCenter(ipos, gInputSignal);
    const float phiColor = gPhiColor * sqrt(max(0.0, epsVariance + variance));
    const float phiDepth = max(sampleCenter.zDerivative, 1e-8) * gStepSize;

    float sumWeight = 1.0;
    float3 sumSignal = sampleCenter.signal;
    float sumVariance = sampleCenter.variance;

    const int radius = 2;
    for (int yy = -radius; yy <= radius; yy++)
    {
        for (int xx = -radius; xx <= radius; xx++)
        {
            const int2 p = ipos + int2(xx, yy) * gStepSize;
            const bool inside = all(greaterThanEqual(p, int2(0, 0))) && all(lessThan(p, screenSize));

            if (inside && (xx != 0 || yy != 0))
            {
                SVGFSample sampleP = FetchSignalSample(gInputSignal, gCompactNormDepth, p);

                const float edgeStopping = ComputeWeight(sampleCenter, sampleP, phiDepth * length(float2(xx, yy)), gPhiNormal, phiColor);
                const float kernel = kernelWeights[abs(xx)] * kernelWeights[abs(yy)];
                const float weight = edgeStopping * kernel;
                
                sumWeight += weight;
                sumSignal += sampleP.signal * weight;
                sumVariance += sampleP.variance * weight * weight;
            }
        }
    }

    out.signal = float4(sumSignal / sumWeight, sumVariance / (sumWeight * sumWeight));
    
    return out;
}
