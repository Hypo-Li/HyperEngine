#include <Common/Binding.slang>
#include <Common/VertexPositionOffset.slang>
#include <Common/MaterialIO.slang>

[MATERIAL_IMPLEMENTATION]

[VERTEX_ATTRIBUTE_GETTERS]

[shader("vertex")]
float4 VertexMain(in VSInput input) : SV_Position
{
    uint instanceIndex = sIndices[getInstanceIndex(input)];
    InstanceData instanceData = sInstanceData[instanceIndex];
    ViewData viewData = sViewData[0];

    float4x4 localToView;
    calculateLocalToView(
        instanceData.localToWorld,
        viewData.worldToViewRot,
        viewData.cameraOrigin,
        localToView
    );

    float3 positionVS = mul(float4(getPosition(input), 1), localToView).xyz;

    // 顶点位置偏移(vertex position offset)按世界空间方向求值, 和 Opaque.slang 的 VertexMain
    // 用同一套约定, 具体语义见 VertexPositionOffset.slang 的注释
    VertexPositionOffsetInput vpoi;
    vpoi.positionLS = getPosition(input);
    vpoi.normalLS = getNormal(input);
    vpoi.time = uFrameData.wrappedTime;
    vpoi.localToWorld = instanceData.localToWorld;
    positionVS += mul(evaluateVertexPositionOffset(vpoi), viewData.worldToViewRot);

    float4 positionCS = mul(float4(positionVS, 1), viewData.viewToClip);
    positionCS *= 1.0 / positionCS.w;

    float3 lightDirWS = viewData.worldToViewRot[2];
    float3 normalWS = mul(getNormal(input), instanceData.localToWorldRot);
    float NoL = dot(normalWS, lightDirWS);
    constexpr float MaxSlopeDepthBias = 1.0;
    float slope = clamp(abs(NoL) > 0 ? sqrt(saturate(1 - NoL * NoL)) / NoL : MaxSlopeDepthBias, 0.0, MaxSlopeDepthBias);
    //float depthBias = PC.depthBias + PC.slopeBias * slope;
    //positionCS.z += depthBias;
    return positionCS;
}

// 不调用 evaluateMaterial()——这是 RenderTagFlags::GeometryOnly 的约定:该 render tag 下静态、
// 不透明、无顶点位置偏移的材质会被重定向到跨材质共享的 shader, 共享 shader 没有材质参数/
// evaluateMaterial 定义, FragmentMain 必须对所有材质都不依赖它们才能保证共享路径和逐材质路径
// 编译出来的行为一致
[shader("fragment")]
void FragmentMain()
{

}
