#include <Common/Binding.slang>

// 点标记: 跟 Icon 一样是屏幕空间恒定像素大小的 billboard(见 VisualizationIcon.slang 顶点偏移那套
// 手法), 但没有图标贴图——fragment shader 里按距离原点丢弃圆外的像素, 画成一个圆点而不是方块
struct VisualizationPointInstance
{
    WorldCoord position;
    float size;
    float _pad0[3];
    float4 color;
    uint pickId;
    uint ignoreDepth;
    uint _pad1[2];
};
[[vk::binding(0, 2)]]
StructuredBuffer<VisualizationPointInstance> gInstances;

struct VisualizationShapePushConstants
{
    uint offset;
};
[[vk::push_constant]]
VisualizationShapePushConstants kPushConstants;

static const float2 kQuadCorners[4] = { float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, 1) };

struct VaryingsPoint
{
    float4 svPosition : SV_Position;
    float2 localUV : TEXCOORD0;
    float4 color : COLOR0;
};

[shader("vertex")]
VaryingsPoint VertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    VisualizationPointInstance instance = gInstances[kPushConstants.offset + instanceId];

    float3 relativePos = calculateRelativeWorldPosition(instance.position, uViewData.cameraOrigin);
    float3 positionVS = mul(relativePos, uViewData.worldToViewRot);
    // 用不带 TAA 抖动的矩阵, 见 Icon.slang 里同样的注释
    float4 centerClip = mul(float4(positionVS, 1), uViewData.viewToClipUnjittered);

    float2 corner = kQuadCorners[vertexId];
    float2 ndcOffset = corner * instance.size / uViewData.resolution.xy * 2.0;

    VaryingsPoint output;
    output.svPosition = centerClip + float4(ndcOffset * centerClip.w, 0, 0);
    // ignoreDepth: 见 Icon.slang 里同名字段的注释, 排序发生在 CPU 侧 build()
    if (instance.ignoreDepth != 0)
        output.svPosition.z = output.svPosition.w;
    output.localUV = corner;
    output.color = instance.color;
    return output;
}

[shader("fragment")]
float4 FragmentMain(VaryingsPoint input) : SV_Target
{
    if (dot(input.localUV, input.localUV) > 1.0)
        discard;
    return input.color;
}
