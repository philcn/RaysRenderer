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
    uint historyLength : SV_TARGET2;
};

PsOut main(float2 texC : TEXCOORD, float4 pos : SV_POSITION)
{
    PsOut out;

    out.signal = gInputSignal[pos.xy];
    out.moments = gPrevMoments[pos.xy].xy;
    out.historyLength = gHistoryLength[pos.xy].x;

    return out;
}
