#include <Common/Binding.slang>

// 世界空间线段——两个端点各自独立换算(不是先算中心再展开), 不需要 billboard
struct VisualizationLineInstance
{
    WorldCoord start;
    WorldCoord end;
    float4 color;
    uint pickId;
    uint ignoreDepth;
    uint _pad0[2];
};
[[vk::binding(0, 2)]]
StructuredBuffer<VisualizationLineInstance> gInstances;

struct VisualizationShapePushConstants
{
    uint offset;
};
[[vk::push_constant]]
VisualizationShapePushConstants kPushConstants;

struct VaryingsLine
{
    float4 svPosition : SV_Position;
    float4 color : COLOR0;
};

[shader("vertex")]
VaryingsLine VertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    VisualizationLineInstance instance = gInstances[kPushConstants.offset + instanceId];
    WorldCoord endpoint = vertexId == 0 ? instance.start : instance.end;

    float3 relativePos = calculateRelativeWorldPosition(endpoint, uViewData.cameraOrigin);
    float3 positionVS = mul(relativePos, uViewData.worldToViewRot);

    VaryingsLine output;
    // 用不带 TAA 抖动的矩阵, 见 Icon.slang 里同样的注释
    output.svPosition = mul(float4(positionVS, 1), uViewData.viewToClipUnjittered);
    // ignoreDepth: 见 Icon.slang 里同名字段的注释, 排序发生在 CPU 侧 build()
    if (instance.ignoreDepth != 0)
        output.svPosition.z = output.svPosition.w;
    output.color = instance.color;
    return output;
}

[shader("fragment")]
float4 FragmentMain(VaryingsLine input) : SV_Target
{
    return input.color;
}
