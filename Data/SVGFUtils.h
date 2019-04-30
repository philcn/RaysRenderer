#ifndef SVGF_UTILS_H
#define SVGF_UTILS_H

int2 getTextureDims(Texture2D tex, uint mip)
{
    uint w, h;
    tex.GetDimensions(w, h);
    return int2(w, h);
}

float3 octToDir(uint octo)
{
    float2 e = float2(f16tof32(octo & 0xFFFF), f16tof32((octo >> 16) & 0xFFFF));
    float3 v = float3(e, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0)
        v.xy = (1.0 - abs(v.yx)) * (step(0.0, v.xy) * 2.0 - (float2)(1.0));
    return normalize(v);
}

uint dirToOct(float3 normal)
{
    float2 p = normal.xy * (1.0 / dot(abs(normal), 1.0.xxx));
    float2 e = normal.z > 0.0 ? p : (1.0 - abs(p.yx)) * (step(0.0, p) * 2.0 - (float2)(1.0));
    return (asuint(f32tof16(e.y)) << 16) + (asuint(f32tof16(e.x)));
}

float normalDistanceCos(float3 n1, float3 n2, float power)
{
    //return pow(max(0.0, dot(n1, n2)), 128.0);
    //return pow( saturate(dot(n1,n2)), power);
    return 1.0f;
}

float normalDistanceTan(float3 a, float3 b)
{
    const float d = max(1e-8, dot(a, b));
    return sqrt(max(0.0, 1.0 - d * d)) / d;
}

struct SVGFSample
{
    float3 signal; float variance;
    float3 normal; float linearZ;
    float zDerivative;
    float luminance;
};

SVGFSample FetchSignalSample(Texture2D signalTexture, Texture2D ndTexture, int2 ipos)
{
    const float4 signal = signalTexture.Load(int3(ipos, 0));
    const float4 nd = ndTexture.Load(int3(ipos, 0));

    SVGFSample s;
    s.signal = signal.rgb;
    s.variance = signal.a;
    s.normal = normalize(octToDir(asuint(nd.x)));
    s.linearZ = nd.y;
    s.zDerivative = nd.z;
    s.luminance = luminance(s.signal);

    return s;
}

float ComputeWeight(SVGFSample sampleCenter, SVGFSample sampleP, float phiDepth, float phiNormal, float phiColor)
{
    const float wNormal = normalDistanceCos(sampleCenter.normal, sampleP.normal, phiNormal);
    const float wZ = (phiDepth == 0) ? 0.0f : abs(sampleCenter.linearZ - sampleP.linearZ) / phiDepth;
    const float wLdirect = abs(sampleCenter.luminance - sampleP.luminance) / phiColor;

    return exp(0.0 - max(wLdirect, 0.0) - max(wZ, 0.0)) * wNormal;
}

#define COMPARE_FUNC2(TYPE) \
bool2 equal(TYPE a, TYPE b)              { return bool2(a.x == b.x, a.y == b.y); } \
bool2 notEqual(TYPE a, TYPE b)           { return bool2(a.x != b.x, a.y != b.y); } \
bool2 lessThan(TYPE a, TYPE b)           { return bool2(a.x <  b.x, a.y <  b.y); } \
bool2 greaterThan(TYPE a, TYPE b)        { return bool2(a.x >  b.x, a.y >  b.y); } \
bool2 lessThanEqual(TYPE a, TYPE b)      { return bool2(a.x <= b.x, a.y <= b.y); } \
bool2 greaterThanEqual(TYPE a, TYPE b)   { return bool2(a.x >= b.x, a.y >= b.y); }

COMPARE_FUNC2(int2)
COMPARE_FUNC2(float2)

#undef COMPARE_FUNC2

#define COMPARE_FUNC3(TYPE) \
bool3 equal(TYPE a, TYPE b)              { return bool3(a.x == b.x, a.y == b.y, a.z == b.z); } \
bool3 notEqual(TYPE a, TYPE b)           { return bool3(a.x != b.x, a.y != b.y, a.z != b.z); } \
bool3 lessThan(TYPE a, TYPE b)           { return bool3(a.x <  b.x, a.y <  b.y, a.z <  b.z); } \
bool3 greaterThan(TYPE a, TYPE b)        { return bool3(a.x >  b.x, a.y >  b.y, a.z >  b.z); } \
bool3 lessThanEqual(TYPE a, TYPE b)      { return bool3(a.x <= b.x, a.y <= b.y, a.z <= b.z); } \
bool3 greaterThanEqual(TYPE a, TYPE b)   { return bool3(a.x >= b.x, a.y >= b.y, a.z >= b.z); }

COMPARE_FUNC3(int3)
COMPARE_FUNC3(float3)

#undef COMPARE_FUNC3

#define COMPARE_FUNC4(TYPE) \
bool4 equal(TYPE a, TYPE b)              { return bool4(a.x == b.x, a.y == b.y, a.z == b.z, a.w == b.w); } \
bool4 notEqual(TYPE a, TYPE b)           { return bool4(a.x != b.x, a.y != b.y, a.z != b.z, a.w != b.w); } \
bool4 lessThan(TYPE a, TYPE b)           { return bool4(a.x <  b.x, a.y <  b.y, a.z <  b.z, a.w <  b.w); } \
bool4 greaterThan(TYPE a, TYPE b)        { return bool4(a.x >  b.x, a.y >  b.y, a.z >  b.z, a.w >  b.w); } \
bool4 lessThanEqual(TYPE a, TYPE b)      { return bool4(a.x <= b.x, a.y <= b.y, a.z <= b.z, a.w <= b.w); } \
bool4 greaterThanEqual(TYPE a, TYPE b)   { return bool4(a.x >= b.x, a.y >= b.y, a.z >= b.z, a.w >= b.w); }

COMPARE_FUNC4(int4)
COMPARE_FUNC4(float4)

#undef COMPARE_FUNC4

#endif
