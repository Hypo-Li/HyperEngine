#include "WireframeCommon.slang"

struct VisualizationWireframeSphereInstance
{
    WorldMatrix transform;
    float radius;
    float _pad0[3];
    float4 color;
    uint pickId;
    uint ignoreDepth;
    uint _pad1[2];
};
[[vk::binding(0, 2)]]
StructuredBuffer<VisualizationWireframeSphereInstance> gInstances;

struct VisualizationShapePushConstants
{
    uint offset;
};
[[vk::push_constant]]
VisualizationShapePushConstants kPushConstants;

[shader("vertex")]
VaryingsWireframe VertexMain(in float3 position : POSITION0, uint instanceId : SV_InstanceID)
{
    VisualizationWireframeSphereInstance instance = gInstances[kPushConstants.offset + instanceId];
    float3 localPos = position * instance.radius;

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
