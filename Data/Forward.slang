import ShaderCommon;
import Shading;
import DefaultVS;

float4 main(VertexOut vOut) : SV_TARGET
{
    ShadingData sd = prepareShadingData(vOut, gMaterial, gCamera.posW);

    ShadingResult sr = evalMaterial(sd, gLights[0], 1);

    float4 color = 0;
    color.rgb += sr.color.rgb;
    color.rgb += sd.emissive;
    color.a = 1;

    return color;
}
