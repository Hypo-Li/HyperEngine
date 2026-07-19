// 把 SHCoeffGather.slang 按 workgroup 分段写出的部分和, 沿 group 维度累加成最终的 9 个卷积后
// 球谐系数, 然后直接在这里按"多项式基"重新打包成 7 个 float4(原来这一步是 SkyLightPass.cpp 里
// CPU 端的 packDiffuseSH 做的, 现在挪到 GPU, readback 时只需要原样拷贝这 7 个 float4)。
// 推导过程(为什么是这么分组的)见 SkyLightPass.cpp 里保留的旧版 packDiffuseSH 注释——
// E(n) = dot(packed[c], float4(n,1)) + dot(packed[c+3], float4(n.x*n.y,n.y*n.z,n.z*n.z,n.x*n.z))
//      + packed[6][c] * (n.x*n.x - n.y*n.y), c = 0/1/2 对应 rgb

[[vk::binding(0, 2)]]
StructuredBuffer<float4> SrcPartialSHCoeff;

[[vk::binding(1, 2)]]
RWStructuredBuffer<float4> DstPackedSHCoeff;

struct ReduceImmediateData
{
    uint groupCount;
};
[[vk::push_constant]]
ReduceImmediateData ImData;

static constexpr float kY00 = 0.2820947918;
static constexpr float kY1 = 0.4886025119;
static constexpr float kY2XY = 1.0925484306;
static constexpr float kY20 = 0.3153915652;
static constexpr float kY22 = 0.5462742153;

// 9 个卷积后系数(每个是 rgb), 先各自沿 group 维度求和, 写进 groupshared 供打包阶段读取
groupshared float3 gSum[9];

[shader("compute")]
[numthreads(9, 1, 1)]
void ComputeMain(uint3 id : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    uint coeffIndex = id.x;

    float3 sum = float3(0.0, 0.0, 0.0);
    for (uint group = 0u; group < ImData.groupCount; ++group)
    {
        sum += SrcPartialSHCoeff[group * 9u + coeffIndex].rgb;
    }
    gSum[coeffIndex] = sum;

    GroupMemoryBarrierWithGroupSync();

    // 9 个线程里只有前 7 个参与打包输出, 各自独立算自己负责的那个 float4, 互不依赖
    if (groupIndex < 7u)
    {
        float3 c0 = gSum[0];
        float3 c1 = gSum[1];
        float3 c2 = gSum[2];
        float3 c3 = gSum[3];
        float3 c4 = gSum[4];
        float3 c5 = gSum[5];
        float3 c6 = gSum[6];
        float3 c7 = gSum[7];
        float3 c8 = gSum[8];

        if (groupIndex < 3u)
        {
            uint channel = groupIndex;
            DstPackedSHCoeff[channel] = float4(
                -kY1 * c3[channel],             // x
                -kY1 * c1[channel],             // y
                 kY1 * c2[channel],              // z
                 kY00 * c0[channel] - kY20 * c6[channel] // 常数(Y0 + Y2,0 拆出来的 "-1" 部分)
            );
        }
        else if (groupIndex < 6u)
        {
            uint channel = groupIndex - 3u;
            DstPackedSHCoeff[groupIndex] = float4(
                 kY2XY * c4[channel],            // xy
                -kY2XY * c5[channel],            // yz
                 3.0 * kY20 * c6[channel],        // z²(Y2,0 拆出来的 "3z²" 部分)
                -kY2XY * c7[channel]              // xz
            );
        }
        else // groupIndex == 6
        {
            DstPackedSHCoeff[6] = float4(kY22 * c8.r, kY22 * c8.g, kY22 * c8.b, 1.0);
        }
    }
}
