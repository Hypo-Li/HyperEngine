#pragma once
#include "MonteCarlo.slang"

// 2^-尾数位数, R11G11B10 的 (R,G,B) 尾数位数是 (6,6,5), 见 UE Quantization.cpp:14-16
static const float3 kR11G11B10QuantizationError = float3(1.0 / 64.0, 1.0 / 64.0, 1.0 / 32.0);

// UE QuantizeFloatColor(Quantization.ush:13-22)
float3 quantizeFloatColor(float3 color, float3 quantizationError, float e)
{
    float3 error = color * quantizationError;
    error = asfloat(asuint(error) & ~0x007FFFFFu);
    return color + error * e;
}

// UE QuantizeForFloatRenderTarget(LumenFloatQuantization.ush:24-29)
float3 quantizeForFloatRenderTarget(float3 color, int3 p)
{
    float e = rand16ToFloat(rand3DPCG16(p).x);
    return quantizeFloatColor(color, kR11G11B10QuantizationError, e);
}
