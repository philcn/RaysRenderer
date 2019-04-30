cbuffer PerPassCB
{
    float gPhiColor;
    float gPhiNormal;
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
    PsOut out;

    out.signal = gInputSignal[pos.xy];

    return out;
}
