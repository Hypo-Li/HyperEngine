#pragma once

struct BxDFData
{
    float NoL;
    float NoV;

};

static constexpr float PI = 3.1415926535897932;

float3 F_Schlick(float VoH, float3 F0)
{
    return F0 + (1.0 - F0) * pow(max(1.0 - VoH, 0.0), 5.0);
}

float3 F_Schlick(float VoH, float3 F0, float3 F90)
{
    return F0 + (F90 - F0) * pow(max(1.0 - VoH, 0.0), 5.0);
}

float D_GGX(float NoH, float a2)
{
    float d = (NoH * NoH) * (a2 - 1.0) + 1.0;
    return clamp(a2 / (PI * d * d), 0.0, 1000.0);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float a2)
{
    float GGXV = NoL * sqrt(NoV * (NoV - NoV * a2) + a2);
    float GGXL = NoV * sqrt(NoL * (NoL - NoL * a2) + a2);
    return saturate(0.5 / (GGXV + GGXL));
}

// 只算镜面反射(Fr)这一部分——从 evaluateIBL 里拆出来, 单独暴露是因为 Translucent.slang
// 想要天光镜面反射, 但漫反射走的是光照体积纹理(不是 packedSH), 没法直接调用下面那个把
// Fd/Fr 绑在一起算的 evaluateIBL()
float3 evaluateIBLSpecular(
    float3 F0,
    float roughness,
    float3 N,
    float3 V,
    float3 R,
    in Texture2D dfgLut,
    in SamplerState lutSampler,
    in TextureCube specularPrefilter,
    in SamplerState prefilterSampler
)
{
    float NoV = max(dot(N, V), 0.0);
    float2 dfg = dfgLut.SampleLevel(lutSampler, float2(NoV, roughness), 0.0).rg;
    float3 E = lerp(dfg.xxx, dfg.yyy, F0);

    float a = roughness * roughness;
    float a2 = a * a;
    uint prefilterWidth, prefilterHeight, prefilterMipCount;
    specularPrefilter.GetDimensions(0, prefilterWidth, prefilterHeight, prefilterMipCount);
    float mip = roughness * (2.0 - roughness) * max(float(prefilterMipCount) - 1.0, 0.0);
    float3 prefilter = specularPrefilter.SampleLevel(prefilterSampler, lerp(R, N, a2), mip).rgb;
    float3 energyCompensation = 1.0 + F0 * (1.0 / dfg.y - 1.0);
    return prefilter * E * energyCompensation;
}

float3 evaluateIBL(
    float3 albedo,
    float roughness,
    float3 F0,
    float3 N,
    float3 V,
    float3 R,
    in float4 packedSH[7],
    in Texture2D dfgLut,
    in SamplerState lutSampler,
    in TextureCube specularPrefilter,
    in SamplerState prefilterSampler
)
{
    float NoV = max(dot(N, V), 0.0);
    float2 dfg = dfgLut.SampleLevel(lutSampler, float2(NoV, roughness), 0.0).rg;
    float3 E = lerp(dfg.xxx, dfg.yyy, F0);

    float4 q1 = float4(N.x * N.y, N.y * N.z, N.z * N.z, N.x * N.z);
    float q2 = N.x * N.x - N.y * N.y;
    float3 Fd = float3(
        dot(packedSH[0], float4(N, 1.0)) + dot(packedSH[3], q1) + packedSH[6][0] * q2,
        dot(packedSH[1], float4(N, 1.0)) + dot(packedSH[4], q1) + packedSH[6][1] * q2,
        dot(packedSH[2], float4(N, 1.0)) + dot(packedSH[5], q1) + packedSH[6][2] * q2
    );
    // packedSH 重建出来的是辐照度, 除以 PI 换算成 Lambertian 出射辐亮度才能乘 albedo
    Fd = max(Fd, float3(0.0)) / PI * albedo * (1.0 - E);

    float3 Fr = evaluateIBLSpecular(F0, roughness, N, V, R, dfgLut, lutSampler, specularPrefilter, prefilterSampler);

    return Fd + Fr;
}