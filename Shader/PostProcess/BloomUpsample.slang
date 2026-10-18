// SIGGRAPH 2014, "Next Generation Post Processing in Call of Duty: Advanced Warfare"(Jorge
// Jimenez)提出的 9-tap tent 升采样滤波: 比直接双线性放大更宽的过滤核, 让相邻 mip 之间的过渡
// 更平滑, 不会在金字塔的每一级边界处露出可见的采样格纹。
//
// 加法混合(mip(N-1) += tent(mipN))原来是靠 ColorBlendState(One, One)做的, 这个 shader 只算
// tent 滤波结果; 迁移成 compute shader 之后 storage image 没有硬件 blend, 改成显式的
// read-modify-write(见下面 PC.additiveBlend 分支), DstTexture 因此必须声明成 RWTexture2D
// (可读可写)而不是只写的 WTexture2D。
//
// weight 是必需的衰减因子, 不是可选的强度旋钮: 降采样阶段每一级 mip 已经写过一份真实的、
// 量级和原图相当的内容, 如果升采样每一步都把 tent 结果原样(权重 1)叠加上去, 越往大分辨率走
// 累加的份数越多, mip1 最终亮度会跟迭代次数(iterations)近似成正比地暴涨, composite 阶段
// 稍微给点强度整个画面就过曝糊成一片。这里按 0.5 的等比衰减叠加(公比 0.5 的等比数列收敛于 2
// 倍原图亮度, 与 iterations 无关), 由 BloomPass.cpp 按"是否最后一步"传入 1.0 或 0.5。

[[vk::binding(0, 2)]]
SamplerState LinearClampSampler;

[[vk::binding(1, 2)]]
Texture2D<float4> SrcTexture;

[[vk::binding(2, 2)]]
[format("rgba16f")]
RWTexture2D<float4> DstTexture;

struct BloomUpsampleConstants
{
    float2 srcTexelSize;
    float radius;
    float weight;
    // 非零表示这一步要叠加到 DstTexture 已有的内容上(降采样阶段写过的真实数据); 最后一步
    // (写从未被写过的专属 mip0)传 0, 直接覆盖, 不读旧值——mip0 里当时是未初始化数据, 读出来
    // 加上去就是脏数据。
    uint additiveBlend;
};
[[vk::push_constant]]
BloomUpsampleConstants PC;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint width, height;
    DstTexture.GetDimensions(width, height);
    if (dispatchThreadID.x >= width || dispatchThreadID.y >= height)
        return;

    float2 uv = (float2(dispatchThreadID.xy) + 0.5) / float2(width, height);
    float2 texel = PC.srcTexelSize * PC.radius;

    float3 topLeft = SrcTexture.SampleLevel(LinearClampSampler, uv + texel * float2(-1.0, -1.0), 0.0).rgb;
    float3 top = SrcTexture.SampleLevel(LinearClampSampler, uv + texel * float2( 0.0, -1.0), 0.0).rgb;
    float3 topRight = SrcTexture.SampleLevel(LinearClampSampler, uv + texel * float2( 1.0, -1.0), 0.0).rgb;
    float3 left = SrcTexture.SampleLevel(LinearClampSampler, uv + texel * float2(-1.0,  0.0), 0.0).rgb;
    float3 center = SrcTexture.SampleLevel(LinearClampSampler, uv, 0.0).rgb;
    float3 right = SrcTexture.SampleLevel(LinearClampSampler, uv + texel * float2( 1.0,  0.0), 0.0).rgb;
    float3 bottomLeft = SrcTexture.SampleLevel(LinearClampSampler, uv + texel * float2(-1.0,  1.0), 0.0).rgb;
    float3 bottom = SrcTexture.SampleLevel(LinearClampSampler, uv + texel * float2( 0.0,  1.0), 0.0).rgb;
    float3 bottomRight = SrcTexture.SampleLevel(LinearClampSampler, uv + texel * float2( 1.0,  1.0), 0.0).rgb;

    float3 result = center * 4.0
        + (top + bottom + left + right) * 2.0
        + (topLeft + topRight + bottomLeft + bottomRight);
    result *= (1.0 / 16.0) * PC.weight;

    if (PC.additiveBlend != 0)
    {
        result += DstTexture[dispatchThreadID.xy].rgb;
    }

    DstTexture.Store(dispatchThreadID.xy, float4(result, 1.0));
}
