#include "WireframeCommon.slang"

struct VisualizationWireframeLightConeInstance
{
    WorldMatrix transform;
    float range;
    float halfAngle;
    float _pad0[2];
    float4 color;
    uint pickId;
    uint ignoreDepth;
    uint _pad1[2];
};
[[vk::binding(0, 2)]]
StructuredBuffer<VisualizationWireframeLightConeInstance> gInstances;

struct VisualizationShapePushConstants
{
    uint offset;
};
[[vk::push_constant]]
VisualizationShapePushConstants kPushConstants;

[shader("vertex")]
VaryingsWireframe VertexMain(in float3 position : POSITION0, uint instanceId : SV_InstanceID)
{
    VisualizationWireframeLightConeInstance instance = gInstances[kPushConstants.offset + instanceId];
    // 单位锥体烘焙在 45 度参考角、range=1, 轴向 -Z(跟聚光灯锥轴约定一致): 轴向缩放 range,
    // 径向缩放 range*tan(实际半角)——跟 DefaultResources.cpp 里
    // createVisualizationWireframeLightConeGeometry() 的注释同一套换算, 非参照角下是近似
    float lateralScale = instance.range * tan(instance.halfAngle);
    float3 localPos = position * float3(lateralScale, lateralScale, instance.range);

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
