import Shading;

shared Texture2D gGBuf0; // WorldPosition
shared Texture2D gGBuf1; // NormalRoughness
shared Texture2D gGBuf2; // Albedo
shared Texture2D gGBuf3; // MotionVector, unused

ShadingData LoadGBuffer(float2 pos)
{
    float3 posW    = gGBuf0.Load(int3(pos.xy, 0)).rgb;
    float4 buf1Val = gGBuf1.Load(int3(pos.xy, 0));
    float3 normalW = buf1Val.rgb;
    float linearRoughness = buf1Val.a;
    float4 albedo  = gGBuf2.Load(int3(pos.xy, 0));

    ShadingData sd = initShadingData();
    sd.posW = posW;
    sd.V = normalize(gCamera.posW - posW);
    sd.N = normalW;
    sd.NdotV = abs(dot(sd.V, sd.N));
    sd.linearRoughness = max(0.08, linearRoughness); // Clamp the roughness so that the BRDF won't explode
    sd.roughness = sd.linearRoughness * sd.linearRoughness;
    sd.diffuse = albedo.rgb;
    sd.specular = float3(0.04); // TODO: handle metalness
    sd.opacity = albedo.a;
    
    return sd;
}
