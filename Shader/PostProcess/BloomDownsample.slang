// SIGGRAPH 2014, "Next Generation Post Processing in Call of Duty: Advanced Warfare"(Jorge
// Jimenez)提出的 13-tap 降采样滤波: 用 5 组各自双线性采样出来的 2x2 盒式滤波结果做加权和,
// 相比朴素的 2x2 box downsample 能大幅压制降采样链路上的 aliasing/闪烁。

[[vk::binding(0, 2)]]
SamplerState LinearClampSampler;

[[vk::binding(1, 2)]]
Texture2D<float4> SrcTexture;

[[vk::binding(2, 2)]]
[format("rgba16f")]
WTexture2D<float4> DstTexture;

struct BloomDownsampleConstants
{
    float2 srcTexelSize;
    uint useKarisAverage;
    float pad;
};
[[vk::push_constant]]
BloomDownsampleConstants PC;

float luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// Karis 2014("Manuka: rendering with maps"配套的 UE4 Bloom 笔记)提出的 firefly 抑制手段:
// 用 1/(1+luma) 做权重, 而不是简单平均——高亮度的孤立像素(过曝的高光点、镜面反射的太阳)
// 权重会被自动压低, 避免它在降采样链路上被放大成一大片扩散的方块状伪影。只在读原始 HDR 场景
// 颜色的第一级降采样(seed level)用, 后续层的输入已经是降采样过的平滑数据, 不再需要
float3 karisAverage(float3 a, float3 b, float3 c, float3 d)
{
    float wa = 1.0 / (1.0 + luminance(a));
    float wb = 1.0 / (1.0 + luminance(b));
    float wc = 1.0 / (1.0 + luminance(c));
    float wd = 1.0 / (1.0 + luminance(d));
    return (a * wa + b * wb + c * wc + d * wd) / max(wa + wb + wc + wd, 1e-5);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint width, height;
    DstTexture.GetDimensions(width, height);
    if (dispatchThreadID.x >= width || dispatchThreadID.y >= height)
        return;

    float2 uv = (float2(dispatchThreadID.xy) + 0.5) / float2(width, height);
    float2 texel = PC.srcTexelSize;

    float3 a = SrcTexture.SampleLevel(LinearClampSampler, uv + texel * float2(-1.0, -1.0), 0.0).rgb;
    float3 b = SrcTexture.SampleLevel(LinearClampSampler, uv + texel * float2( 0.0, -1.0), 0.0).rgb;
    float3 c = SrcTexture.SampleLevel(LinearClampSampler, uv + texel * float2( 1.0, -1.0), 0.0).rgb;
    float3 d = SrcTexture.SampleLevel(LinearClampSampler, uv + texel * float2(-0.5, -0.5), 0.0).rgb;
    float3 e = SrcTexture.SampleLevel(LinearClampSampler, uv + texel * float2( 0.5, -0.5), 0.0).rgb;
    float3 f = SrcTexture.SampleLevel(LinearClampSampler, uv + texel * float2(-1.0,  0.0), 0.0).rgb;
    float3 g = SrcTexture.SampleLevel(LinearClampSampler, uv, 0.0).rgb;
    float3 h = SrcTexture.SampleLevel(LinearClampSampler, uv + texel * float2( 1.0,  0.0), 0.0).rgb;
    float3 i = SrcTexture.SampleLevel(LinearClampSampler, uv + texel * float2(-0.5,  0.5), 0.0).rgb;
    float3 j = SrcTexture.SampleLevel(LinearClampSampler, uv + texel * float2( 0.5,  0.5), 0.0).rgb;
    float3 k = SrcTexture.SampleLevel(LinearClampSampler, uv + texel * float2(-1.0,  1.0), 0.0).rgb;
    float3 l = SrcTexture.SampleLevel(LinearClampSampler, uv + texel * float2( 0.0,  1.0), 0.0).rgb;
    float3 m = SrcTexture.SampleLevel(LinearClampSampler, uv + texel * float2( 1.0,  1.0), 0.0).rgb;

    float3 result;
    if (PC.useKarisAverage != 0)
    {
        float3 groupCenter = karisAverage(d, e, i, j);
        float3 groupTopLeft = karisAverage(a, b, g, f);
        float3 groupTopRight = karisAverage(b, c, h, g);
        float3 groupBottomLeft = karisAverage(f, g, l, k);
        float3 groupBottomRight = karisAverage(g, h, m, l);
        result = groupCenter * 0.5 + (groupTopLeft + groupTopRight + groupBottomLeft + groupBottomRight) * 0.125;
    }
    else
    {
        float3 groupCenter = (d + e + i + j) * 0.25;
        float3 groupTopLeft = (a + b + g + f) * 0.25;
        float3 groupTopRight = (b + c + h + g) * 0.25;
        float3 groupBottomLeft = (f + g + l + k) * 0.25;
        float3 groupBottomRight = (g + h + m + l) * 0.25;
        result = groupCenter * 0.5 + (groupTopLeft + groupTopRight + groupBottomLeft + groupBottomRight) * 0.125;
    }

    DstTexture.Store(dispatchThreadID.xy, float4(result, 1.0));
}
