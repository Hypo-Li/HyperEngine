// 对应 UE ScreenProbeFixupBordersCS(LumenScreenProbeFiltering.usf:1081-1107), 见 Document/Lumen_Reflections_Plan.md §3.6
#include "../Common/Binding.slang"
#include "ScreenProbeCommon.slang"

[[vk::binding(0, 2)]]
Texture2D<float> ScreenProbeSceneDepth;

[[vk::binding(1, 2)]]
StructuredBuffer<uint> AdaptiveScreenProbeTotalAndTileCounts;

[[vk::binding(2, 2)]]
Texture2D<float4> ScreenProbeRadiance;

[[vk::binding(3, 2)]]
[format("r11f_g11f_b10f")]
RWTexture2D<float4> ScreenProbeRadianceWithBorder;

struct ScreenProbeFixupBordersPushConstants
{
    uint atlasWidth;
    uint numUniformProbes;
    uint maxNumAdaptiveProbes;
};
[[vk::push_constant]]
ScreenProbeFixupBordersPushConstants PC;

// 一个线程对应一个带边框的辐射度 texel, numthreads 字面量必须等于 kScreenProbeRadianceWithBorderResolution
[shader("compute")]
[numthreads(10, 10, 1)]
void ComputeMain(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    uint2 atlasCoord = groupId.xy;
    uint2 texelCoordWithBorder = groupThreadId.xy;
    uint probeIndex = atlasCoord.y * PC.atlasWidth + atlasCoord.x;

    uint numProbes = getNumScreenProbes(PC.numUniformProbes, loadNumAdaptiveScreenProbes(AdaptiveScreenProbeTotalAndTileCounts), PC.maxNumAdaptiveProbes);
    if (probeIndex >= numProbes || atlasCoord.x >= PC.atlasWidth)
        return;

    float3 radiance = float3(0.0);
    if (ScreenProbeSceneDepth[atlasCoord] > 0.0)
    {
        uint2 texelCoord = octahedralMapWrapBorder(
            texelCoordWithBorder, kScreenProbeRadianceWithBorderResolution, kScreenProbeRadianceBorderSize);
        radiance = ScreenProbeRadiance[atlasCoord * kScreenProbeGatherOctahedronResolution + texelCoord].rgb;
    }

    uint2 outputCoord = atlasCoord * kScreenProbeRadianceWithBorderResolution + texelCoordWithBorder;
    ScreenProbeRadianceWithBorder[outputCoord] = float4(
        quantizeForFloatRenderTarget(radiance, int3(int2(outputCoord), int(screenProbeFrameIndex() % 8u))), 1.0);
}
