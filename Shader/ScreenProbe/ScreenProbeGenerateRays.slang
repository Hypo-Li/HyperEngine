// 结构化重要性采样(M7.3: GenerateRays), 对应 UE ScreenProbeGenerateRaysCS
// (LumenScreenProbeImportanceSampling.usf:306-495), NumLevels 固定为 1(跟 UE 默认值一致,
// 只做一轮细分不递归), 见 Document/Lumen_ScreenProbeGather_Plan.md §9。合并 BRDF PDF(M7.1)×
// Lighting PDF(M7.2), 64 条光线按合并 PDF 排序, 排序数组底部低于阈值的 3 条报废, 名额转给
// 排序数组顶部(对称往里数)当前最高 PDF 的那一条, 细分成 2x2 共 4 个子方向。产出的是重定向表——
// 槽位数量/布局不变(还是原生 8x8), 每个槽位存"这条光线最终该用哪个源纹素+源细分等级",
// 留给 M7.4(Trace 接线)消费。
//
// 跟 UE 的刻意偏差:
// 1. 合并阶段"这个 mergeIndex 三元组是否报废"的判定结果不用 InterlockedAdd 原子计数——这个引擎
//    的 Slang WGSL 后端已经踩过 InterlockedAdd 三参数重载编不过的坑(见 ScreenProbeAdaptivePlacement.slang
//    顶部注释), 改成每个 mergeIndex 组的代表线程(mergeThreadIndex==0)写一个 groupshared 标记
//    数组, 单线程串行求和——跟 ScreenProbeBRDFPDF.slang/ScreenProbeLightingPDF.slang 的 64 元素
//    规约同一个套路, 结果完全等价(哪个 mergeIndex 报废是确定性的, 不依赖累加顺序, 不需要原子性)。
// 2. 不做 UE 的 UNIFORM_RAY_DISTRIBUTION_DEBUG/SERIAL_REFERENCE_DEBUG 调试分支、不做
//    BRDF_PDF_SPHERICAL_HARMONIC=0 的离散纹理路径(项目已确认走 SH 默认路径)。
#include "../Common/Binding.slang"
#include "../Common/SHCommon.slang"
#include "ScreenProbeCommon.slang"

[[vk::binding(0, 2)]]
Texture2D<float> ScreenProbeSceneDepth;

[[vk::binding(1, 2)]]
StructuredBuffer<uint> AdaptiveScreenProbeTotalAndTileCounts;

[[vk::binding(2, 2)]]
StructuredBuffer<float> BRDFProbabilityDensityFunctionSH;

[[vk::binding(3, 2)]]
Texture2D<float> ScreenProbeLightingPDF;

[[vk::binding(4, 2)]]
[format("r32ui")]
RWTexture2D<uint> RWStructuredImportanceSampledRayInfosForTracing;

struct ScreenProbeGenerateRaysPushConstants
{
    uint atlasWidth;
    uint numUniformProbes;
    uint maxNumAdaptiveProbes;
};
[[vk::push_constant]]
ScreenProbeGenerateRaysPushConstants PC;

// 跟 ScreenProbeBRDFPDF.slang 里的同名 buffer 平铺布局一致
static const uint kNumSHCoefficients = 9;

ThreeBandSH readBRDFPDFSH(uint probeIndex)
{
    uint baseOffset = probeIndex * kNumSHCoefficients;
    ThreeBandSH sh;
    sh.v0 = float4(
        BRDFProbabilityDensityFunctionSH[baseOffset + 0], BRDFProbabilityDensityFunctionSH[baseOffset + 1],
        BRDFProbabilityDensityFunctionSH[baseOffset + 2], BRDFProbabilityDensityFunctionSH[baseOffset + 3]);
    sh.v1 = float4(
        BRDFProbabilityDensityFunctionSH[baseOffset + 4], BRDFProbabilityDensityFunctionSH[baseOffset + 5],
        BRDFProbabilityDensityFunctionSH[baseOffset + 6], BRDFProbabilityDensityFunctionSH[baseOffset + 7]);
    sh.v2 = BRDFProbabilityDensityFunctionSH[baseOffset + 8];
    return sh;
}

// 排序阶段的临时打包: x = packRayInfo(纹素坐标, 等级), y = asuint(PDF), 跟 UE PackRaySortInfo/
// UnpackRaySortInfo(LumenScreenProbeImportanceSampling.usf:206-217)同一个思路, 只是坐标+等级
// 复用最终输出那 16 位格式, 不需要另外的更宽打包(细分一次后纹素坐标最大 15, 6 位够用)
uint2 packRaySortInfo(uint2 texelCoord, uint level, float pdf)
{
    return uint2(packRayInfo(texelCoord, level), asuint(pdf));
}

void unpackRaySortInfo(uint2 packed, out uint2 texelCoord, out uint level, out float pdf)
{
    unpackRayInfo(packed.x, texelCoord, level);
    pdf = asfloat(packed.y);
}

static const uint kNumMergeGroups = (64u + 2u) / 3u;

groupshared uint2 gUnsortedRays[64];
groupshared uint2 gSortedRays[64];
groupshared uint gMergeFlag[kNumMergeGroups];
groupshared uint gNumRaysToSubdivide;

// numthreads 要求字面量, 跟 kScreenProbeTracingOctahedronResolution 手动保持一致
[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    uint2 atlasCoord = groupId.xy;
    uint2 traceTexelCoord = groupThreadId.xy;
    uint probeIndex = atlasCoord.y * PC.atlasWidth + atlasCoord.x;
    uint2 traceBufferCoord = atlasCoord * kScreenProbeTracingOctahedronResolution + traceTexelCoord;
    uint threadIndex = groupThreadId.y * 8u + groupThreadId.x;

    uint numProbes = getNumScreenProbes(PC.numUniformProbes, loadNumAdaptiveScreenProbes(AdaptiveScreenProbeTotalAndTileCounts), PC.maxNumAdaptiveProbes);
    if (probeIndex >= numProbes || atlasCoord.x >= PC.atlasWidth)
        return;

    float probeDepth = ScreenProbeSceneDepth[atlasCoord];
    if (!(probeDepth > 0.0))
    {
        RWStructuredImportanceSampledRayInfosForTracing[traceBufferCoord] = packRayInfo(traceTexelCoord, kScreenProbeImportanceSamplingUniformLevel);
        return;
    }

    // 第一步: 每个线程算自己这条原生方向的合并 PDF——BRDF PDF 在这条光线实际方向上求值(不是
    // M7.1 调试可视化那个固定方向), Lighting PDF 直接查表(跟这条光线同一个纹素, 不用 SH)
    ThreeBandSH brdfSH = readBRDFPDFSH(probeIndex);
    float2 probeUV = (float2(traceTexelCoord) + 0.5) / float(kScreenProbeTracingOctahedronResolution);
    float3 direction = equiAreaSphericalMapping(probeUV);
    float brdfPDF = max(dotSH3(brdfSH, shBasisFunction3(direction)), 0.0);

    float lightingPDF = ScreenProbeLightingPDF[traceBufferCoord];
    bool notCulledByBRDF = brdfPDF >= kScreenProbeImportanceSamplingMinPDFToTrace;
    float combinedPDF = brdfPDF * (lightingPDF * float(kScreenProbeTracingOctahedronResolution * kScreenProbeTracingOctahedronResolution));
    if (notCulledByBRDF)
        combinedPDF = max(combinedPDF, kScreenProbeImportanceSamplingMinPDFToTrace);

    gUnsortedRays[threadIndex] = packRaySortInfo(traceTexelCoord, kScreenProbeImportanceSamplingUniformLevel, combinedPDF);

    GroupMemoryBarrierWithGroupSync();

    // 第二步: O(N^2) 并行计数排序, 按 PDF 升序(数有多少个别人比自己小, 直接算出自己该在第几位;
    // 同值按原下标大的排后面保证稳定性, 跟 UE 一致)
    {
        uint2 mineTexelCoord;
        uint mineLevel;
        float minePDF;
        unpackRaySortInfo(gUnsortedRays[threadIndex], mineTexelCoord, mineLevel, minePDF);

        uint numSmaller = 0u;
        for (uint other = 0u; other < 64u; ++other)
        {
            uint2 otherTexelCoord;
            uint otherLevel;
            float otherPDF;
            unpackRaySortInfo(gUnsortedRays[other], otherTexelCoord, otherLevel, otherPDF);

            if (otherPDF < minePDF || (otherPDF == minePDF && other > threadIndex))
                numSmaller++;
        }

        gSortedRays[numSmaller] = gUnsortedRays[threadIndex];
    }

    GroupMemoryBarrierWithGroupSync();

    // 第三步: 只做一轮细分——排序数组最低 PDF 的 3 条(mergeIndex 组)如果都低于阈值, 报废,
    // 名额转给排序数组顶部(对称往里数)当前最高 PDF 的那一条, 细分成 2x2 共 4 个子方向
    {
        uint mergeThreadIndex = threadIndex % 3u;
        uint mergeIndex = threadIndex / 3u;
        uint rayIndexToRefine = uint(max(int(64) - int(mergeIndex) - 1, 0));
        uint rayIndexToMerge2 = mergeIndex * 3u + 2u;

        bool groupQualifies = false;
        if (rayIndexToMerge2 < rayIndexToRefine)
        {
            uint2 texelCoord2;
            uint level2;
            float pdf2;
            unpackRaySortInfo(gSortedRays[rayIndexToMerge2], texelCoord2, level2, pdf2);
            groupQualifies = pdf2 < kScreenProbeImportanceSamplingMinPDFToTrace;
        }

        if (groupQualifies)
        {
            uint2 originalTexelCoord;
            uint originalLevel;
            float originalPDF;
            unpackRaySortInfo(gSortedRays[rayIndexToRefine], originalTexelCoord, originalLevel, originalPDF);

            uint2 subOffset = uint2((mergeThreadIndex + 1u) % 2u, (mergeThreadIndex + 1u) / 2u);
            gSortedRays[threadIndex] = packRaySortInfo(originalTexelCoord * 2u + subOffset, originalLevel - 1u, 0.0);
        }

        if (mergeThreadIndex == 0u)
            gMergeFlag[mergeIndex] = groupQualifies ? 1u : 0u;
    }

    GroupMemoryBarrierWithGroupSync();

    if (threadIndex == 0u)
    {
        uint count = 0u;
        for (uint i = 0u; i < kNumMergeGroups; ++i)
            count += gMergeFlag[i];
        gNumRaysToSubdivide = count;
    }

    GroupMemoryBarrierWithGroupSync();

    // 报废掉的三元组数量决定了有几条"最高 PDF"的原方向也要变成细分后的第 4 个(0,0)子方向,
    // 从排序数组顶部往里数, 数量正好对应上面报废掉的三元组数
    if (threadIndex < gNumRaysToSubdivide)
    {
        uint rayIndexToSubdivide = 64u - threadIndex - 1u;
        uint2 originalTexelCoord;
        uint originalLevel;
        float originalPDF;
        unpackRaySortInfo(gSortedRays[rayIndexToSubdivide], originalTexelCoord, originalLevel, originalPDF);
        gSortedRays[rayIndexToSubdivide] = packRaySortInfo(originalTexelCoord * 2u, originalLevel - 1u, 0.0);
    }

    GroupMemoryBarrierWithGroupSync();

    // 第四步: 写出重定向表——槽位数量/布局不变(还是 traceTexelCoord 对应的原生格子), 每个槽位
    // 存排序结果里同一位置那条光线最终该用的(源纹素, 源等级)
    {
        uint2 finalTexelCoord;
        uint finalLevel;
        float finalPDF;
        unpackRaySortInfo(gSortedRays[threadIndex], finalTexelCoord, finalLevel, finalPDF);
        RWStructuredImportanceSampledRayInfosForTracing[traceBufferCoord] = packRayInfo(finalTexelCoord, finalLevel);
    }
}
