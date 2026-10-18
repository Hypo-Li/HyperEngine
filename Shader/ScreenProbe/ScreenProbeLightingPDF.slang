// 结构化重要性采样(M7.2: Lighting PDF), 对应 UE ScreenProbeComputeLightingProbabilityDensityFunctionCS
// (LumenScreenProbeImportanceSampling.usf:37-153, PROBE_RADIANCE_HISTORY 分支)。跟 ScreenProbeBRDFPDF.slang
// 一起无条件跑在 Placement 之后、Trace 之前, 见 Document/Lumen_ScreenProbeGather_Plan.md §9.2。
//
// 跟 UE 的刻意偏差:
// 1. 不持久化 HistoryScreenProbeTranslatedWorldPosition, 改成持久化上一帧探针线性深度, 现算
//    view space 位置——ScreenProbeCommon.slang 的 reprojectPrevViewPositionToCurrent 注释里有
//    详细对照, UE 这处比较本身没有做 PrevPreViewTranslation 修正, 两帧参照系原点不一致。
// 2. 探针自己的屏幕位置重投影用 Velocity 纹理(跟本引擎"统一走 Velocity, 不分静态/动态"的既有
//    偏差一致, 见 ScreenProbeCommon.slang 顶部注释第 4 条), 不是 UE 的纯相机运动 + 动态像素分支。
// 3. 历史资源是单缓冲(读者在 Placement 之后, 写者在 Filtering 产出这一帧结果之后, 读写顺序
//    由调用位置保证), 不是 UE 的每帧提升 RDG 纹理为外部资源那种效果等价的"新建替换旧的"。
// 4. 只对统一探针做历史重投影, 自适应探针没有跨帧稳定身份, 直接退化成 PDF=1。
// 5. 没有 Radiance Cache, 4 个邻居没有全部通过共面测试时退化成 PDF=1, 不做它那个 fallback 分支。
#include "../Common/Binding.slang"
#include "../Common/BindingAttributes.slang"
#include "../Common/GBufferEncoding.slang"
#include "ScreenProbeCommon.slang"

[[vk::binding(0, 2)]]
Texture2D<float> ScreenProbeSceneDepth;

[[vk::binding(1, 2)]]
Texture2D<float2> ScreenProbeWorldNormal;

[[vk::binding(2, 2)]]
[UnfilterableFloat]
Texture2D<float4> VelocityImage;

[[vk::binding(3, 2)]]
StructuredBuffer<uint> AdaptiveScreenProbePosAndTileIndices;

[[vk::binding(4, 2)]]
StructuredBuffer<uint> AdaptiveScreenProbeTotalAndTileCounts;

[[vk::binding(5, 2)]]
Texture2D<float> ScreenProbeLightingHistorySceneDepth;

[[vk::binding(6, 2)]]
Texture2D<float4> ScreenProbeLightingHistoryRadiance;

[[vk::binding(7, 2)]]
[format("r16f")]
RWTexture2D<float> RWLightingProbabilityDensityFunction;

struct ScreenProbeLightingPDFPushConstants
{
    uint2 viewRectMin;
    uint2 viewExtent;
    uint2 tileGridSize;
    uint atlasWidth;
    uint numUniformProbes;
    uint maxNumAdaptiveProbes;
    uint historyValid;
};
[[vk::push_constant]]
ScreenProbeLightingPDFPushConstants PC;

float luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// 探针内 64 个方向的归一化用, 单线程串行求和——跟 ScreenProbeBRDFPDF.slang 的 64 元素规约同一个套路
groupshared float sharedPDF[64];
groupshared float sharedPDFSum;

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
        RWLightingProbabilityDensityFunction[traceBufferCoord] = 1.0;
        return;
    }

    float pdf = 1.0;

    if (PC.historyValid != 0 && probeIndex < PC.numUniformProbes)
    {
        uint2 screenPosition = getScreenProbeScreenPosition(
            probeIndex, atlasCoord, PC.numUniformProbes, PC.viewRectMin, PC.viewExtent, screenProbeFrameIndex(), AdaptiveScreenProbePosAndTileIndices);
        float2 screenUV = (float2(screenPosition) + 0.5) * uViewData.resolution.zw;

        float3 normalWS = octDecode(ScreenProbeWorldNormal[atlasCoord]);
        float3 normalVS = mul(normalWS, uViewData.worldToViewRot);
        float3 probeViewPosition = screenProbeViewPositionAtDepth(screenUV, probeDepth, uViewData);

        // 探针自己的屏幕位置按 Velocity 重投影到上一帧屏幕坐标
        float4 encodedVelocity = VelocityImage[screenPosition];
        float3 velocityDelta = decodeVelocity(encodedVelocity);
        float2 screenPositionNDC = float2(screenUV.x * 2.0 - 1.0, 1.0 - screenUV.y * 2.0);
        float2 historyNDC = screenPositionNDC - velocityDelta.xy;
        float2 historyUV = float2(historyNDC.x * 0.5 + 0.5, 0.5 - historyNDC.y * 0.5);

        float2 halfTexel = 0.5 * uViewData.resolution.zw;
        float2 historyMinUV = float2(PC.viewRectMin) * uViewData.resolution.zw - halfTexel;
        float2 historyMaxUV = float2(PC.viewRectMin + PC.viewExtent) * uViewData.resolution.zw + halfTexel;

        if (all(historyUV >= historyMinUV) && all(historyUV <= historyMaxUV))
        {
            float2 historyScreenPositionPixels = historyUV * uViewData.resolution.xy;
            float2 unclampedHistoryTileCoord = getScreenTileCoordF(historyScreenPositionPixels, PC.viewRectMin, screenProbePrevFrameIndex());

            float3 historyRadiance = float3(0.0, 0.0, 0.0);
            float historyWeight = 0.0;

            for (uint y = 0; y < 2u; ++y)
            {
                for (uint x = 0; x < 2u; ++x)
                {
                    uint2 neighborTileCoord = uint2(clamp(
                        unclampedHistoryTileCoord + float2(x, y), float2(0, 0), float2(PC.tileGridSize) - 1.0));

                    float neighborDepth = ScreenProbeLightingHistorySceneDepth[neighborTileCoord];
                    if (!(neighborDepth > 0.0))
                        continue;

                    uint2 neighborPrevScreenPosition = getClampedUniformScreenProbeScreenPosition(
                        neighborTileCoord, PC.viewRectMin, PC.viewExtent, screenProbePrevFrameIndex());
                    float2 neighborPrevScreenUV = (float2(neighborPrevScreenPosition) + 0.5) * uViewData.resolution.zw;
                    float3 neighborPrevViewPosition = prevScreenProbeViewPositionAtDepth(neighborPrevScreenUV, neighborDepth, uViewData);
                    float3 neighborViewPosition = reprojectPrevViewPositionToCurrent(neighborPrevViewPosition, uViewData);

                    float planeDistance = calculatePlaneDistance(probeViewPosition, normalVS, neighborViewPosition);
                    float relativeDepthDifference = planeDistance / probeDepth;
                    float positionWeight = exp2(kScreenProbeInterpolationDepthWeight * relativeDepthDifference * relativeDepthDifference) > 0.1 ? 1.0 : 0.0;

                    uint2 historyRadianceCoord = neighborTileCoord * kScreenProbeGatherOctahedronResolution + traceTexelCoord;
                    float preExposureCorrection = getPreExposure(uViewData) / max(uViewData.prevPreExposure, 0.0001);
                    historyRadiance += ScreenProbeLightingHistoryRadiance[historyRadianceCoord].rgb * (preExposureCorrection * positionWeight);
                    historyWeight += positionWeight;
                }
            }

            // 全有全无: 4 个邻居必须全部通过共面测试才采用历史混合值, 少一个就整块退化成 flat 1,
            // 跟 UE 没有 Radiance Cache 时的行为一致(LumenScreenProbeImportanceSampling.usf:113-119)
            if (historyWeight >= 3.5)
                pdf = luminance(historyRadiance / historyWeight);
        }
    }

    pdf = max(pdf, 0.0);
    sharedPDF[threadIndex] = pdf;

    GroupMemoryBarrierWithGroupSync();

    // UE 把 64 条光线的原始 Lighting PDF 归一化成"占比"(加总等于 1)才写出去(
    // LumenScreenProbeImportanceSampling.usf:150-191), 不是原始亮度——GenerateRays(M7.3)按这份
    // 归一化后的值跟 BRDF PDF 相乘
    if (threadIndex == 0u)
    {
        float sum = 0.0;
        for (uint i = 0u; i < 64u; ++i)
            sum += sharedPDF[i];
        sharedPDFSum = sum;
    }

    GroupMemoryBarrierWithGroupSync();

    RWLightingProbabilityDensityFunction[traceBufferCoord] = pdf / max(sharedPDFSum, 0.0001);
}
