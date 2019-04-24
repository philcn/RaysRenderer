import ShaderCommon;
import Shading;
import DefaultVS;

cbuffer PerFrameCB
{
    float2 gRenderTargetDim;
};

struct PsOut
{
    float4 position : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float4 albedo : SV_TARGET2;
    float2 motion : SV_TARGET3;
};

PsOut main(VertexOut vOut, float4 pixelCrd : SV_POSITION)
{
    PsOut out;

    ShadingData sd = prepareShadingData(vOut, gMaterial, gCamera.posW);
    out.position = float4(sd.posW, 1.0);
    out.normal = float4(sd.N, sd.linearRoughness);
    out.albedo = float4(sd.diffuse, sd.opacity);
    out.motion = calcMotionVector(pixelCrd.xy, vOut.prevPosH, gRenderTargetDim);

    return out;
}
