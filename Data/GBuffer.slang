import ShaderCommon;
import Shading;
import DefaultVS;

cbuffer PerFrameCB
{
    float2 gRenderTargetDim;
};

struct GBuffer
{
    float4 position : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float4 albedo : SV_TARGET2;
    float4 motion : SV_TARGET3; // .rg motion vector, .b position derivative, .a normal derivative
    float4 linearZ : SV_TARGET4; // .r linear Z, .g max Z derivative, .b last frame Z, .a obj space normal
    float4 compactNormDepth : SV_TARGET5; // .r world normal, .g linear Z, .b Z derivative
};

// A simple utility to convert a float to a 2-component octohedral representation packed into one uint
uint DirToOct(float3 normal)
{
	float2 p = normal.xy * (1.0 / dot(abs(normal), 1.0.xxx));
	float2 e = normal.z > 0.0 ? p : (1.0 - abs(p.yx)) * (step(0.0,p)*2.0-(float2)(1.0)); 
	return (asuint(f32tof16(e.y)) << 16) + (asuint(f32tof16(e.x)));
}

GBuffer main(VertexOut vOut, float4 pixelCrd : SV_POSITION)
{
    ShadingData sd = prepareShadingData(vOut, gMaterial, gCamera.posW);

	// The 'linearZ' buffer
	float linearZ    = vOut.posH.z * vOut.posH.w;
	float maxChangeZ = max(abs(ddx(linearZ)), abs(ddy(linearZ)));
	float objNorm    = asfloat(DirToOct(normalize(vOut.normalW))); // world normal instead of object normal
	float4 svgfLinearZOut = float4(linearZ, maxChangeZ, vOut.prevPosH.z, objNorm);

	// The 'motion vector' buffer
	float2 svgfMotionVec = calcMotionVector(pixelCrd.xy, vOut.prevPosH, gRenderTargetDim) + float2(gCamera.jitterX, -gCamera.jitterY);
	float2 posNormFWidth = float2(length(fwidth(sd.posW)), length(fwidth(sd.N))); 
	float4 svgfMotionVecOut = float4(svgfMotionVec, posNormFWidth);

    GBuffer out;
    out.position = float4(sd.posW, 1.0);
    out.normal = float4(sd.N, sd.linearRoughness);
    out.albedo = float4(sd.diffuse, sd.opacity);
    out.motion = svgfMotionVecOut;
    out.linearZ = svgfLinearZOut;

	// A compacted buffer containing discretizied normal, depth, depth derivative
	out.compactNormDepth = float4(asfloat(DirToOct(sd.N)), linearZ, maxChangeZ, 0.0f);

    return out;
}
