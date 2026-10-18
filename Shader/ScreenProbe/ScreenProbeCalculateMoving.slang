// 对应 UE ScreenProbeCalculateMovingCS(LumenScreenProbeFiltering.usf:1007-1050), 把 64 个 gather texel 的
// ScreenProbeTraceMoving 规约成一个探针级的 ScreenProbeMoving, 见 Document/Lumen_ScreenProbeGather_Plan.md §6.9。
#include "../Common/Binding.slang"
#include "ScreenProbeCommon.slang"

[[vk::binding(0, 2)]]
Texture2D<float> ScreenProbeSceneDepth;

[[vk::binding(1, 2)]]
StructuredBuffer<uint> AdaptiveScreenProbeTotalAndTileCounts;

[[vk::binding(2, 2)]]
Texture2D<float> ScreenProbeTraceMoving;

[[vk::binding(3, 2)]]
[format("r8")]
RWTexture2D<float> ScreenProbeMoving;

struct ScreenProbeCalculateMovingPushConstants
{
    uint atlasWidth;
    uint numUniformProbes;
    uint maxNumAdaptiveProbes;
};
[[vk::push_constant]]
ScreenProbeCalculateMovingPushConstants PC;

static const uint kTexelCount = kScreenProbeGatherOctahedronResolution * kScreenProbeGatherOctahedronResolution;

groupshared float SharedTexelMoving[kTexelCount];

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

    if (!(ScreenProbeSceneDepth[atlasCoord] > 0.0))
    {
        if (all(texelCoord == uint2(0, 0)))
            ScreenProbeMoving[atlasCoord] = 0.0;
        return;
    }

    uint threadIndex = texelCoord.y * kScreenProbeGatherOctahedronResolution + texelCoord.x;
    SharedTexelMoving[threadIndex] = ScreenProbeTraceMoving[atlasCoord * kScreenProbeGatherOctahedronResolution + texelCoord];
    GroupMemoryBarrierWithGroupSync();

    if (threadIndex == 0)
    {
        float total = 0.0;
        for (uint i = 0; i < kTexelCount; ++i)
            total += SharedTexelMoving[i];
        ScreenProbeMoving[atlasCoord] = total / float(kTexelCount);
    }
}
