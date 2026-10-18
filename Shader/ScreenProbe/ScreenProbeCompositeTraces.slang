// 对应 UE ScreenProbeCompositeTracesWithScatterCS(LumenScreenProbeFiltering.usf:30-181), 见 Document/Lumen_ScreenProbeGather_Plan.md §6.4、§6.9。
// M7.5: 接入 GenerateRays(M7.3)的重定向表(STRUCTURED_IMPORTANCE_SAMPLING 分支, 同文件 65-106
// 行)——细分过的光线只覆盖 1/4 立体角, 散射到 gather 图集时要按 (GatherRes/MipSize)² 降权,
// 否则一条细分光线会被当成跟未细分光线同等权重, 多算了 4 倍能量。散射目标坐标也要用重定向后的
// 纹素坐标(乘同一个分辨率比例), 不能再用原生 texelCoord。
#include "../Common/Binding.slang"
#include "ScreenProbeCommon.slang"

[[vk::binding(0, 2)]]
Texture2D<float> ScreenProbeSceneDepth;

[[vk::binding(1, 2)]]
StructuredBuffer<uint> AdaptiveScreenProbePosAndTileIndices;

[[vk::binding(2, 2)]]
StructuredBuffer<uint> AdaptiveScreenProbeTotalAndTileCounts;

[[vk::binding(3, 2)]]
Texture2D<float4> TraceRadiance;

[[vk::binding(4, 2)]]
Texture2D<uint> TraceHit;

[[vk::binding(5, 2)]]
Texture2D<float4> BlueNoiseVec2Texture;

[[vk::binding(6, 2)]]
[format("r11f_g11f_b10f")]
RWTexture2D<float4> ScreenProbeRadiance;

[[vk::binding(7, 2)]]
[format("r8")]
RWTexture2D<float> ScreenProbeHitDistance;

[[vk::binding(8, 2)]]
[format("r8")]
RWTexture2D<float> ScreenProbeTraceMoving;

[[vk::binding(9, 2)]]
Texture2D<uint> ScreenProbeRayInfos;

struct ScreenProbeCompositeTracesPushConstants
{
    uint2 viewRectMin;
    uint2 viewExtent;
    uint atlasWidth;
    uint numUniformProbes;
    uint maxNumAdaptiveProbes;
    float maxRayIntensity;
    float maxWorldTraceDistance;
};
[[vk::push_constant]]
ScreenProbeCompositeTracesPushConstants PC;

static const uint kTexelCount = kScreenProbeGatherOctahedronResolution * kScreenProbeGatherOctahedronResolution;

// 0-2: 辐射度 RGB 定点累加, 3: bMoving 定点加权和, 4: 最小命中距离(用 IEEE-754 非负浮点的位模式
// 排序跟数值排序一致这一点, 靠 InterlockedMin 在 uint 上间接实现浮点 min)
groupshared Atomic<uint> SharedAccumulators[kTexelCount][5];

// numthreads 要求字面量, 跟 kScreenProbeGatherOctahedronResolution 手动保持一致
[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    uint2 atlasCoord = groupId.xy;
    uint2 texelCoord = groupThreadId.xy;
    uint probeIndex = atlasCoord.y * PC.atlasWidth + atlasCoord.x;

    uint numProbes = getNumScreenProbes(PC.numUniformProbes, loadNumAdaptiveScreenProbes(AdaptiveScreenProbeTotalAndTileCounts), PC.maxNumAdaptiveProbes);
    if (probeIndex >= numProbes || atlasCoord.x >= PC.atlasWidth)
        return;

    uint2 gatherTexelCoord = atlasCoord * kScreenProbeGatherOctahedronResolution + texelCoord;

    if (!(ScreenProbeSceneDepth[atlasCoord] > 0.0))
    {
        ScreenProbeRadiance[gatherTexelCoord] = float4(0.0, 0.0, 0.0, 1.0);
        ScreenProbeHitDistance[gatherTexelCoord] = encodeProbeHitDistanceForFiltering(PC.maxWorldTraceDistance, PC.maxWorldTraceDistance);
        ScreenProbeTraceMoving[gatherTexelCoord] = 0.0;
        return;
    }

    uint threadIndex = texelCoord.y * kScreenProbeGatherOctahedronResolution + texelCoord.x;
    SharedAccumulators[threadIndex][0].store(0u);
    SharedAccumulators[threadIndex][1].store(0u);
    SharedAccumulators[threadIndex][2].store(0u);
    SharedAccumulators[threadIndex][3].store(0u);
    SharedAccumulators[threadIndex][4].store(asuint(PC.maxWorldTraceDistance));
    GroupMemoryBarrierWithGroupSync();

    uint2 screenPosition = getScreenProbeScreenPosition(
        probeIndex, atlasCoord, PC.numUniformProbes, PC.viewRectMin, PC.viewExtent, screenProbeFrameIndex(), AdaptiveScreenProbePosAndTileIndices);
    uint2 screenTileCoord = getScreenTileCoord(screenPosition, PC.viewRectMin, screenProbeFrameIndex());
    uint rayDirectionFrameIndex = screenProbeFrameIndex() % kScreenProbeMaxRayDirections;
    float2 probeTexelCenter = getRadiosityProbeTexelCenter(BlueNoiseVec2Texture, rayDirectionFrameIndex, screenTileCoord);

    // 查重定向表: 细分过的光线(mipSize=16)只覆盖原生纹素 1/4 的立体角, sampleWeight 按
    // (GatherRes/MipSize)² 降权——4 个子方向权重加总正好等于被细分前那一个原生方向的权重, 总能量守恒
    uint2 rayTexelCoord;
    uint rayLevel;
    unpackRayInfo(ScreenProbeRayInfos[gatherTexelCoord], rayTexelCoord, rayLevel);
    uint mipSize = kScreenProbeMaxImportanceSamplingOctahedronResolution >> rayLevel;
    float gatherToMipRatio = float(kScreenProbeGatherOctahedronResolution) / float(mipSize);
    float sampleWeight = gatherToMipRatio * gatherToMipRatio;

    float3 lighting = TraceRadiance[gatherTexelCoord].rgb * sampleWeight;
    float maxLighting = max(lighting.x, max(lighting.y, lighting.z));
    if (maxLighting > PC.maxRayIntensity)
        lighting *= PC.maxRayIntensity / maxLighting;

    // 散射目标坐标必须用重定向后的纹素坐标(乘同一个分辨率比例), 细分光线的实际方向已经不在
    // 原生纹素中心附近了, 用 texelCoord 会散到错误的位置
    float2 gatherTexelPosition = (float2(rayTexelCoord) + probeTexelCenter) * gatherToMipRatio - 0.5;
    float2 random = getRadiosityProbeTexelCenter(
        BlueNoiseVec2Texture, rayDirectionFrameIndex + (kBlueNoiseVec2Dimensions.z - 1u) / 2u, screenTileCoord);
    int2 quantizedTexelCoord = int2(floor(gatherTexelPosition)) + int2(frac(gatherTexelPosition) >= random);
    uint2 scatterTexelCoord = octahedralMapWrapBorder(
        uint2(quantizedTexelCoord + 1), kScreenProbeGatherOctahedronResolution + 2u, 1u);

    const float kMaxValuePerThread = 4294967296.0 / float(kTexelCount);
    uint3 quantizedLighting = uint3(lighting * (kMaxValuePerThread / PC.maxRayIntensity));
    uint scatterIndex = scatterTexelCoord.y * kScreenProbeGatherOctahedronResolution + scatterTexelCoord.x;
    SharedAccumulators[scatterIndex][0].add(quantizedLighting.x);
    SharedAccumulators[scatterIndex][1].add(quantizedLighting.y);
    SharedAccumulators[scatterIndex][2].add(quantizedLighting.z);

    ProbeRayDistance probeRayDistance = decodeProbeRayDistance(TraceHit[gatherTexelCoord]);
    SharedAccumulators[scatterIndex][3].add(probeRayDistance.moving ? uint(sampleWeight * kMaxValuePerThread) : 0u);
    SharedAccumulators[scatterIndex][4].min(asuint(probeRayDistance.hitDistance));
    GroupMemoryBarrierWithGroupSync();

    float3 accumulated = float3(
        SharedAccumulators[threadIndex][0].load(), SharedAccumulators[threadIndex][1].load(), SharedAccumulators[threadIndex][2].load());
    float3 composited = accumulated * (PC.maxRayIntensity * float(kTexelCount) / 4294967296.0);
    float texelMoving = float(SharedAccumulators[threadIndex][3].load()) / kMaxValuePerThread;
    float minHitDistance = asfloat(SharedAccumulators[threadIndex][4].load());

    ScreenProbeRadiance[gatherTexelCoord] = float4(
        quantizeForFloatRenderTarget(composited, int3(int2(gatherTexelCoord), int(screenProbeFrameIndex() % 8u))), 1.0);
    ScreenProbeHitDistance[gatherTexelCoord] = encodeProbeHitDistanceForFiltering(minHitDistance, PC.maxWorldTraceDistance);
    ScreenProbeTraceMoving[gatherTexelCoord] = texelMoving;
}
