#include "WireframeCommon.slang"

struct VisualizationWireframeBoxInstance
{
    WorldMatrix transform;
    float3 halfExtents;
    float _pad0[1];
    float4 color;
    uint pickId;
    uint ignoreDepth;
    uint _pad1[2];
};
[[vk::binding(0, 2)]]
StructuredBuffer<VisualizationWireframeBoxInstance> gInstances;

struct VisualizationShapePushConstants
{
    uint offset;
};
[[vk::push_constant]]
VisualizationShapePushConstants kPushConstants;

[shader("vertex")]
VaryingsWireframe VertexMain(in float3 position : POSITION0, uint instanceId : SV_InstanceID)
{
    VisualizationWireframeBoxInstance instance = gInstances[kPushConstants.offset + instanceId];
    // 单位盒烘焙在 -0.5~0.5, halfExtents*2 才是实际边长
    float3 localPos = position * instance.halfExtents * 2.0;

    VaryingsWireframe output;
    output.svPosition = applyIgnoreDepth(computeWireframeClipPosition(localPos, instance.transform), instance.ignoreDepth);
    output.color = instance.color;
    return output;
}

[shader("fragment")]
float4 FragmentMain(VaryingsWireframe input) : SV_Target
{
    return input.color;
}
