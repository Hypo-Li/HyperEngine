#include "WireframeCommon.slang"

struct VisualizationWireframeCylinderInstance
{
    WorldMatrix transform;
    float radius;
    float halfHeight;
    float _pad0[2];
    float4 color;
    uint pickId;
    uint ignoreDepth;
    uint _pad1[2];
};
[[vk::binding(0, 2)]]
StructuredBuffer<VisualizationWireframeCylinderInstance> gInstances;

struct VisualizationShapePushConstants
{
    uint offset;
};
[[vk::push_constant]]
VisualizationShapePushConstants kPushConstants;

[shader("vertex")]
VaryingsWireframe VertexMain(in float3 position : POSITION0, uint instanceId : SV_InstanceID)
{
    VisualizationWireframeCylinderInstance instance = gInstances[kPushConstants.offset + instanceId];
    // 单位圆柱烘焙在 radius=1、halfHeight=1, 轴向 +Z: 径向(XY)按 radius 缩放, 轴向(Z)按
    // halfHeight 缩放
    float3 localPos = position * float3(instance.radius, instance.radius, instance.halfHeight);

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
