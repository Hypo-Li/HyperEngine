#pragma once

// 图集存归一化有向距离: 0.5 是字形边缘, 大于 0.5 在字形内部。sdfType 是 FontSdfType 的数值
// (0 Sdf, 1 Msdf, 2 Mtsdf), 对应 Font 参数展开出来的 <名字>SdfType

float fontChannelMedian(float3 value)
{
    return max(min(value.x, value.y), min(max(value.x, value.y), value.z));
}

float sampleFontDistance(Texture2DArray atlas, SamplerState atlasSampler, float page, float2 uv, int sdfType)
{
    const float4 texel = atlas.Sample(atlasSampler, float3(uv, page));
    return sdfType == 0 ? texel.r : fontChannelMedian(texel.rgb);
}

// spreadUv 是 Font 参数展开出来的 <名字>SpreadUv; 只能在 fragment 里调用
float fontScreenPxRange(float spreadUv, float2 uv)
{
    const float2 screenTexelsPerUv = 1.0 / max(fwidth(uv), float2(1e-6));
    return max(spreadUv * (screenTexelsPerUv.x + screenTexelsPerUv.y), 1.0);
}

float fontDistanceToCoverage(float distance, float screenPxRange)
{
    return saturate((distance - 0.5) * screenPxRange + 0.5);
}

// 一步得到字形覆盖率, 只能在 fragment 里调用。edgeOffsetPx 是把字形边缘向外推的屏幕像素数(负数向内收),
// 加粗或描边用: 描边覆盖率 = 带偏移的覆盖率 - 不带偏移的覆盖率
float sampleFontCoverage(
    Texture2DArray atlas, SamplerState atlasSampler, float page, float2 uv, int sdfType, float spreadUv, float edgeOffsetPx)
{
    const float distance = sampleFontDistance(atlas, atlasSampler, page, uv, sdfType);
    const float screenPxRange = fontScreenPxRange(spreadUv, uv);
    return saturate((distance - 0.5) * screenPxRange + 0.5 + edgeOffsetPx);
}

float sampleFontCoverage(
    Texture2DArray atlas, SamplerState atlasSampler, float page, float2 uv, int sdfType, float spreadUv)
{
    return sampleFontCoverage(atlas, atlasSampler, page, uv, sdfType, spreadUv, 0.0);
}
