#pragma once

#include "ReflectionCommon.slang"

float luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// 去噪器色调映射空间——UE LumenReflectionDenoiserCommon.ush:12-40,压低高亮度样本对邻域钳位/
// 双边滤波权重的影响,色调映射范围取 UE 默认值 10(r.Lumen.Reflections.DenoiserTonemapRange)
static const float kReflectionDenoiserOneOverTonemapRange = 1.0 / 10.0;

float3 rgbToDenoiserSpace(float3 v)
{
    if (!isReflectionLightingValid(v))
        return v;

    float weight = 1.0 / (1.0 + luminance(v) * kReflectionDenoiserOneOverTonemapRange);
    return v * weight;
}

float3 denoiserSpaceToRGB(float3 v)
{
    if (!isReflectionLightingValid(v))
        return v;

    float invWeight = 1.0 / (1.0 - luminance(v) * kReflectionDenoiserOneOverTonemapRange);
    return v * invWeight;
}

// 全分辨率追踪(降采样系数 1)按粗糙度缩短累积帧数,UE LumenReflectionDenoiserCommon.ush:42-48
float getMaxFramesAccumulated(float maxFrames, float roughness)
{
    return lerp(2.0, maxFrames, saturate(roughness / 0.05));
}

// UE FastMathThirdParty.ush:108-114
float acosFast(float x)
{
    float absX = abs(x);
    float res = (-0.156583 * absX + 0.5 * PI) * sqrt(1.0 - absX);
    return x >= 0.0 ? res : PI - res;
}

// UE Common.ush:1187-1192, 只对 x >= 0 有效
float atanFast(float x)
{
    float a = x < 1.0 ? x : 1.0 / x;
    float offset = x < 1.0 ? 0.0 : 0.5 * PI;
    float sign = x < 1.0 ? 1.0 : -1.0;
    return offset + sign * (((-0.130234 * a - 0.0954105) * a + 1.00712) * a - 0.00001203333);
}

// UE StochasticLightingCommon.ush:229-234
float getSpecularLobeHalfAngle(float roughness)
{
    const float fractionOfPreservedEnergy = 0.75;
    return atanFast(pow2(roughness) * fractionOfPreservedEnergy / (1.0 - fractionOfPreservedEnergy));
}

half packNumFramesAccumulated(half numFrames)
{
    return numFrames / half(32.0);
}

half unpackNumFramesAccumulated(half packed)
{
    return packed * half(32.0);
}
