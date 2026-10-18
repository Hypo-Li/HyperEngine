// 把"统一探针数量(CPU 已知常量)+ 实际分配出去的自适应探针数量(只有 GPU 知道)"合成一份
// DrawIndirectCommand.instanceCount, 供 ScreenProbeVisualizer.slang 的 instanced draw 用——
// 只分配出去的自适应探针才画, 没分配到的槽位是瞬态图集复用上一次内容的脏数据, 不能指望
// "深度 <=0 就是无效"这种判断去跳过它们, 见 ScreenProbeVisualizerPass.cpp 的注释
#include "../Common/Binding.slang"
#include "ScreenProbeCommon.slang"

[[vk::binding(0, 2)]]
StructuredBuffer<uint> AdaptiveScreenProbeTotalAndTileCounts;

struct DrawIndirectCommand
{
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
};
[[vk::binding(1, 2)]]
RWStructuredBuffer<DrawIndirectCommand> ScreenProbeDrawIndirect;

struct ScreenProbeBuildDrawIndirectPushConstants
{
    uint numUniformProbes;
    uint maxNumAdaptiveProbes;
};
[[vk::push_constant]]
ScreenProbeBuildDrawIndirectPushConstants PC;

[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    ScreenProbeDrawIndirect[0].instanceCount = getNumScreenProbes(PC.numUniformProbes, loadNumAdaptiveScreenProbes(AdaptiveScreenProbeTotalAndTileCounts), PC.maxNumAdaptiveProbes);
}
