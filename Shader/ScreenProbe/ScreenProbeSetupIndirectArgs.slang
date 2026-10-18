// 按实际探针数写 GroupPerProbe 间接 dispatch 参数, 跟 UE SetupAdaptiveProbeIndirectArgsCS
// (LumenScreenProbeGather.usf:318-332)的第 0 项一致: 图集宽度不变, 只发覆盖 numProbes 所需的行数。
#include "../Common/Binding.slang"
#include "ScreenProbeCommon.slang"

[[vk::binding(0, 2)]]
StructuredBuffer<uint> AdaptiveScreenProbeTotalAndTileCounts;

[[vk::binding(1, 2)]]
RWStructuredBuffer<uint> ScreenProbeIndirectArgs;

struct ScreenProbeSetupIndirectArgsPushConstants
{
    uint atlasWidth;
    uint numUniformProbes;
    uint maxNumAdaptiveProbes;
};
[[vk::push_constant]]
ScreenProbeSetupIndirectArgsPushConstants PC;

[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint numProbes = getNumScreenProbes(PC.numUniformProbes, loadNumAdaptiveScreenProbes(AdaptiveScreenProbeTotalAndTileCounts), PC.maxNumAdaptiveProbes);
    ScreenProbeIndirectArgs[0] = PC.atlasWidth;
    ScreenProbeIndirectArgs[1] = (numProbes + PC.atlasWidth - 1u) / PC.atlasWidth;
    ScreenProbeIndirectArgs[2] = 1u;
}
