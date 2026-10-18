#include <Common/Binding.slang>

// 图标 billboard: 屏幕空间恒定像素大小, 不随距离缩放——4 个顶点(SV_VertexID, 不绑定顶点/索引
// buffer)现算成一个正对相机的 quad, 在裁剪空间给顶点加一个跟 centerClip.w 成正比的偏移, 让
// 透视除法之后的屏幕像素偏移量恒定、不随深度变化(标准的"屏幕空间常量像素大小" billboard 手法)
struct VisualizationIconInstance
{
    WorldCoord position;
    float size;
    float _pad0[3];
    float4 atlasUV;
    float4 color;
    uint pickId;
    uint ignoreDepth;
    uint _pad1[2];
};
[[vk::binding(0, 2)]]
StructuredBuffer<VisualizationIconInstance> gInstances;

[[vk::binding(1, 2)]]
Texture2D<float4> IconAtlasTexture;

[[vk::binding(2, 2)]]
SamplerState IconAtlasSampler;

struct VisualizationShapePushConstants
{
    uint offset; // gInstances 里这一帧这个类型条目的起始下标, 元素下标不是字节偏移
};
[[vk::push_constant]]
VisualizationShapePushConstants kPushConstants;

// 顶点顺序对应 Triangle_Strip: 左下/右下/左上/右上
static const float2 kQuadCorners[4] = { float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, 1) };
static const float2 kQuadUVs[4] = { float2(0, 1), float2(1, 1), float2(0, 0), float2(1, 0) };

struct VaryingsIcon
{
    float4 svPosition : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

[shader("vertex")]
VaryingsIcon VertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    VisualizationIconInstance instance = gInstances[kPushConstants.offset + instanceId];

    float3 relativePos = calculateRelativeWorldPosition(instance.position, uViewData.cameraOrigin);
    float3 positionVS = mul(relativePos, uViewData.worldToViewRot);
    // 用不带 TAA 抖动的矩阵——这个 pass 排在 TAA resolve 之后画, 没有后续步骤把抖动平均掉,
    // 用带抖动的 viewToClip 会导致每帧跟着亚像素偏移抖动
    float4 centerClip = mul(float4(positionVS, 1), uViewData.viewToClipUnjittered);

    float2 ndcOffset = kQuadCorners[vertexId] * instance.size / uViewData.resolution.xy * 2.0;

    VaryingsIcon output;
    output.svPosition = centerClip + float4(ndcOffset * centerClip.w, 0, 0);
    // ignoreDepth: 深度测试/写入都开着(见 build() 里 orderByDepthGroup 的排序说明), 但把裁剪
    // 空间深度钉死在 reversed-Z 下最近的值(透视除法后 NDC z=1), 保证不被真实场景/DepthTested
    // 条目挡住——同为 ignoreDepth 的条目之间打平, 靠 CPU 侧按距离排序、近的最后画来分胜负
    if (instance.ignoreDepth != 0)
        output.svPosition.z = output.svPosition.w;
    output.uv = lerp(instance.atlasUV.xy, instance.atlasUV.zw, kQuadUVs[vertexId]);
    output.color = instance.color;
    return output;
}

[shader("fragment")]
float4 FragmentMain(VaryingsIcon input) : SV_Target
{
    return IconAtlasTexture.Sample(IconAtlasSampler, input.uv) * input.color;
}
