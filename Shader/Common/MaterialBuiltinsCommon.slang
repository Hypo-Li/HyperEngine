#pragma once

namespace Material
{
    float3 unpackNormal(float2 xy)
    {
        float2 signedXY = xy * 2.0 - 1.0;
        float z = sqrt(saturate(1.0 - dot(signedXY, signedXY)));
        return float3(signedXY, z);
    }

    float remap(float value, float inputLow, float inputHigh, float targetLow, float targetHigh)
    {
        return (value - inputLow) / (inputHigh - inputLow) * (targetHigh - targetLow) + targetLow;
    }

    MatUV bumpOffset(MatUV uv, float2 cameraVectorTS, float height, float heightRatio, float referencePlane)
    {
        return uv + cameraVectorTS * (heightRatio * (height - referencePlane));
    }

    // 图集存归一化有向距离: 0.5 是字形边缘, 大于 0.5 在字形内部; sdfType 0 为 SDF, 1/2 为 MSDF/MTSDF
    float fontChannelMedian(float3 value)
    {
        return max(min(value.x, value.y), min(max(value.x, value.y), value.z));
    }

    float sampleFontDistance(MatTexture2DArray atlas, SamplerState atlasSampler, float page, MatUV uv, int sdfType)
    {
        const float4 texel = sampleTexture2DArray(atlas, atlasSampler, uv, page);
        return sdfType == 0 ? texel.r : fontChannelMedian(texel.rgb);
    }

    float fontScreenPxRange(float spreadUv, MatUV uv)
    {
        const float2 screenTexelsPerUv = 1.0 / max(fwidth(uv), float2(1e-6));
        return max(spreadUv * (screenTexelsPerUv.x + screenTexelsPerUv.y), 1.0);
    }

    // edgeOffsetPx 把字形边缘向外推若干屏幕像素(负数向内收), 描边覆盖率 = 带偏移的覆盖率 - 不带偏移的覆盖率
    float sampleFontCoverage(
        MatTexture2DArray atlas, SamplerState atlasSampler, float page, MatUV uv, int sdfType, float spreadUv, float edgeOffsetPx)
    {
        const float distance = sampleFontDistance(atlas, atlasSampler, page, uv, sdfType);
        const float screenPxRange = fontScreenPxRange(spreadUv, uv);
        return saturate((distance - 0.5) * screenPxRange + 0.5 + edgeOffsetPx);
    }

    float sampleFontCoverage(
        MatTexture2DArray atlas, SamplerState atlasSampler, float page, MatUV uv, int sdfType, float spreadUv)
    {
        return sampleFontCoverage(atlas, atlasSampler, page, uv, sdfType, spreadUv, 0.0);
    }
}
