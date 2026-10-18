#include "Cubemap.slang"
#include "SHCommon.slang"

// 把一张 cubemap 的低分辨率 mip 子资源 view 投影到 9 个 3 阶球谐系数上。face size 和
// 每行 workgroup 数由调用方传入。原来的写法用 InterlockedAdd 把所有 workgroup 的结果原子
// 累加到同一个 buffer 里, 但 float 原子在大部分 WebGPU 平台上不可用, 所以拆成两个 pass:
// 这一份只负责"计算 + workgroup 内规约", 每个 workgroup 把自己的 9 个系数写到 DstPartialSHCoeff
// 里独占的一段, 不同 workgroup 之间互不冲突, 不需要任何原子操作; 真正跨 workgroup 的求和交给
// SHCoeffReduce.slang 做

[[vk::binding(0, 2)]]
SamplerState LinearSampler;

[[vk::binding(1, 2)]]
TextureCube<float4> SrcCubemap;

[[vk::binding(2, 2)]]
RWStructuredBuffer<float4> DstPartialSHCoeff;

struct GatherImmediateData
{
    uint faceSize;
    uint groupsPerRow;
};
[[vk::push_constant]]
GatherImmediateData ImData;

// L0/L1(前 4 项)的基函数常量已经挪进 SHCommon.slang::shBasisFunction(), 这里只留
// L2(5 个系数)——天光专属的完整 2 阶, radiosity 那边只要 L0/L1, 不在 SHCommon.slang 范围内
static constexpr float kSHConstantsL2[5] = {
    1.0925484306,  // Y2-2: sqrt(15/(4π))
    -1.0925484306, // Y2-1
    0.3153915652,  // Y20: sqrt(5/(16π))
    -1.0925484306, // Y21
    0.5462742153    // Y22: sqrt(15/(16π))
};

static constexpr float A0 = PI;
static constexpr float A1 = 2.0 * PI / 3.0;
static constexpr float A2 = PI / 4.0;

static constexpr uint kThreadsPerGroup = 64u; // numthreads(8, 8, 1)

// 9 个系数 x 64 个线程(numthreads(8,8,1))各自的贡献, 规约时按 [coeff * kThreadsPerGroup + groupIndex] 访问
groupshared float3 gContrib[576];

float areaElement(float u, float v)
{
    float invLen = rsqrt(1.0 + u * u + v * v);
    return atan2(u * v * invLen, invLen);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    float invSize = 1.0 / float(ImData.faceSize);
    uint face = dispatchThreadId.z;
    uint x = dispatchThreadId.x;
    uint y = dispatchThreadId.y;
    bool validTexel = x < ImData.faceSize && y < ImData.faceSize;
    float u = (x + 0.5) * invSize;
    float v = (y + 0.5) * invSize;
    float3 dir = faceUVToCubemapDir(face, float2(u, v));

    // 立体角权重（以 texel 边界计算）
    float u0 = ( (float(x)    ) * invSize ) * 2.0 - 1.0;
    float v0 = ( (float(y)    ) * invSize ) * 2.0 - 1.0;
    float u1 = ( (float(x) + 1) * invSize ) * 2.0 - 1.0;
    float v1 = ( (float(y) + 1) * invSize ) * 2.0 - 1.0;
    float solidAngle =
          areaElement(u0, v0)
        - areaElement(u0, v1)
        - areaElement(u1, v0)
        + areaElement(u1, v1);

    // 传进来的 view 已经是单独一级 mip, 这里始终采样它的 LOD 0
    float3 L = SrcCubemap.SampleLevel(LinearSampler, dir, 0.0).rgb;

    // SH 基——L0/L1(前 4 项)复用 SHCommon.slang, L2(后 5 项)天光专属, 留在这里
    float x1 = dir.x, y1 = dir.y, z1 = dir.z;
    float4 basisL0L1 = shBasisFunction(dir);
    float basis[9];
    basis[0] = basisL0L1.x;
    basis[1] = basisL0L1.y;
    basis[2] = basisL0L1.z;
    basis[3] = basisL0L1.w;
    basis[4] = kSHConstantsL2[0] * x1 * y1;
    basis[5] = kSHConstantsL2[1] * y1 * z1;
    basis[6] = kSHConstantsL2[2] * (3.0*z1*z1 - 1.0);
    basis[7] = kSHConstantsL2[3] * x1 * z1;
    basis[8] = kSHConstantsL2[4] * (x1*x1 - y1*y1);

    float Al[9] = float[9](A0, A1, A1, A1, A2, A2, A2, A2, A2);

    // 每线程贡献, 先写进 groupshared, 准备做 workgroup 内规约
    for (int i = 0; i < 9; ++i)
    {
        gContrib[i * kThreadsPerGroup + groupIndex] = validTexel
            ? L * (basis[i] * solidAngle * Al[i])
            : float3(0.0, 0.0, 0.0);
    }

    GroupMemoryBarrierWithGroupSync();

    // 标准的树形规约, 不依赖 subgroup 大小, 每一步的 barrier 对所有线程无条件执行,
    // 只有参与求和的那部分线程被 if 挡住, 满足 WGSL 对 barrier 必须在 uniform 控制流下的要求。
    // 循环起始值/终止条件必须是字面量, 不能写成从 kThreadsPerGroup 这类 static constexpr
    // 全局变量派生的表达式——Slang 把它们降级成 WGSL 的 var<private>, Tint 的 uniformity
    // 分析没法证明这类"私有变量"恒定不变, 会把依赖它的循环条件当成"可能非 uniform", 进而
    // 判定循环体里的 GroupMemoryBarrierWithGroupSync() 不满足 uniform 控制流要求而拒绝编译
    // (和 RadixSort/Spine.slang 里同一类问题、同样的规避方式)
    for (uint stride = 32u; stride > 0u; stride >>= 1u)
    {
        if (groupIndex < stride)
        {
            for (int i = 0; i < 9; ++i)
            {
                gContrib[i * kThreadsPerGroup + groupIndex] += gContrib[i * kThreadsPerGroup + groupIndex + stride];
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0u)
    {
        uint groupLinearIndex = groupId.z * (ImData.groupsPerRow * ImData.groupsPerRow)
            + groupId.y * ImData.groupsPerRow + groupId.x;
        for (int i = 0; i < 9; ++i)
        {
            DstPartialSHCoeff[groupLinearIndex * 9u + i] = float4(gContrib[i * kThreadsPerGroup], 0.0);
        }
    }
}
