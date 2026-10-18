#include <Common/Binding.slang>
#include <Common/VertexPositionOffset.slang>
#include <Common/MaterialIO.slang>

[[vk::binding(0, 3)]]
StructuredBuffer<InstanceData> sInstanceData;

[MATERIAL_IMPLEMENTATION]

[VERTEX_LAYOUT_IMPLEMENTATION]

// PickPass(Source/Editor/Editor/RenderPass/PickPass.cpp)每次执行都会给这次请求里的每个候选
// 单独发一次 draw call(不做实例合批——同一次 pick 请求的候选数量本来就很小, 不值得为了合批
// 再引入一份跟 sIndices 平行的逐实例 pickId buffer, 换来绑定层面的复杂度), pickId 直接由这个
// push constant 传, 跟 ShadowDepth.slang 的 ShadowDepthBiasParams 是同一个惯例
struct EditorPickPushConstants
{
    uint pickId;
};
[[vk::push_constant]]
EditorPickPushConstants PC;

// MASKED 材质要点中真正不透明的像素才算命中, 跟 ShadowDepth.slang 处理阴影穿帮的理由一样——
// 复用同一套 VSOuput/MaterialInput 拼装, 只在 BLEND_MODE_MASKED 下才会被编译到
#if BLEND_MODE == BLEND_MODE_MASKED

struct VSOuput
{
    float4 svPosition : SV_Position;
    float3 positionVS : TEXCOORD0;
#if VERTEX_HAS_COLOR
    float4 color : TEXCOORD1;
#endif
#if VERTEX_HAS_TEXCOORD0
    float2 texCoord0 : TEXCOORD2;
#endif
#if VERTEX_HAS_TEXCOORD1
    float2 texCoord1 : TEXCOORD3;
#endif
#if VERTEX_HAS_TEXCOORD2
    float2 texCoord2 : TEXCOORD4;
#endif
#if VERTEX_HAS_TEXCOORD3
    float2 texCoord3 : TEXCOORD5;
#endif
};

// 片元入口要把 flat 成员单独声明成参数: 放进入口接收的结构里, Slang 输出 WGSL 时会在那份结构拷贝上
// 残留没有 location 的 @interpolate(flat), 被 WebGPU 拒绝
struct VSStageOutput
{
    VSOuput varyings;
    nointerpolation uint instanceIndex : TEXCOORD6;
};

MaterialInput makeMaterialInput(in VSOuput vsout, in uint instanceIndex)
{
    MaterialInput mi;
    mi.twoSidedSign = 1.0;
    mi.normalWS = float3(0, 0, 1);
    mi.tangentWS = float4(1, 0, 0, 1);
#if VERTEX_HAS_COLOR
    mi.color = vsout.color;
#else
    mi.color = float4(1);
#endif

#if VERTEX_HAS_TEXCOORD0
    mi.texCoords[0] = vsout.texCoord0;
#else
    mi.texCoords[0] = float2(0, 0);
#endif
#define LAST_TEXCOORD_INDEX 0

#if VERTEX_HAS_TEXCOORD1
    mi.texCoords[1] = vsout.texCoord1;
#undef LAST_TEXCOORD_INDEX
#define LAST_TEXCOORD_INDEX 1
#else
    mi.texCoords[1] = mi.texCoords[LAST_TEXCOORD_INDEX];
#endif

#if VERTEX_HAS_TEXCOORD2
    mi.texCoords[2] = vsout.texCoord2;
#undef LAST_TEXCOORD_INDEX
#define LAST_TEXCOORD_INDEX 2
#else
    mi.texCoords[2] = mi.texCoords[LAST_TEXCOORD_INDEX];
#endif

#if VERTEX_HAS_TEXCOORD3
    mi.texCoords[3] = vsout.texCoord3;
#undef LAST_TEXCOORD_INDEX
#define LAST_TEXCOORD_INDEX 3
#else
    mi.texCoords[3] = mi.texCoords[LAST_TEXCOORD_INDEX];
#endif

    mi.viewDirWS = float3(0, 0, 1);
    mi.positionRWS = float4(mul(uViewData.worldToViewRot, vsout.positionVS), -vsout.positionVS.z);
    mi.relativeWorldToObject = calculateRelativeWorldToObject(
        sInstanceData[instanceIndex].worldToObjectRS,
        sInstanceData[instanceIndex].objectToWorld.getTranslation(),
        uViewData.cameraOrigin
    );
    mi.time = uFrameData.wrappedTime;
    return mi;
}

#endif // BLEND_MODE == BLEND_MODE_MASKED

[shader("vertex")]
#if BLEND_MODE == BLEND_MODE_MASKED
VSStageOutput VertexMain(in VSInput input)
#else
float4 VertexMain(in VSInput input) : SV_Position
#endif
{
    uint instanceIndex = getInstanceIndex(input);
    InstanceData instanceData = sInstanceData[instanceIndex];
    const VertexGeometry geo = getVertexGeometry(input);

    float4x4 objectToView;
    calculateObjectToView(
        instanceData.objectToWorld,
        uViewData.worldToViewRot,
        uViewData.cameraOrigin,
        objectToView
    );

    float3 positionVS = mul(float4(geo.position, 1), objectToView).xyz;

    // 跟 ShadowDepth.slang/Opaque.slang 同一套约定: 顶点位置偏移按世界空间方向求值
    VertexPositionOffsetInput vpoi;
    vpoi.positionOS = geo.position;
    vpoi.normalOS = geo.normal;
    vpoi.time = uFrameData.wrappedTime;
    vpoi.objectToWorld = instanceData.objectToWorld;
    positionVS += mul(evaluateVertexPositionOffset(vpoi), uViewData.worldToViewRot);

    float4 positionCS = mul(float4(positionVS, 1), uViewData.viewToClip);

#if BLEND_MODE == BLEND_MODE_MASKED
    VSOuput output;
    output.svPosition = positionCS;
    output.positionVS = positionVS;
#if VERTEX_HAS_COLOR
    output.color = getColor(input);
#endif
#if VERTEX_HAS_TEXCOORD0
    output.texCoord0 = getTexCoord0(input);
#endif
#if VERTEX_HAS_TEXCOORD1
    output.texCoord1 = getTexCoord1(input);
#endif
#if VERTEX_HAS_TEXCOORD2
    output.texCoord2 = getTexCoord2(input);
#endif
#if VERTEX_HAS_TEXCOORD3
    output.texCoord3 = getTexCoord3(input);
#endif

    VSStageOutput stageOutput;
    stageOutput.varyings = output;
    stageOutput.instanceIndex = instanceIndex;
    return stageOutput;
#else
    return positionCS;
#endif
}

[shader("fragment")]
#if BLEND_MODE == BLEND_MODE_MASKED
uint FragmentMain(in VSOuput vsout, nointerpolation uint instanceIndex : TEXCOORD6) : SV_Target
{
    MaterialInput mi = makeMaterialInput(vsout, instanceIndex);
    MaterialOutput mo;
    evaluateMaterial(mi, mo);
    if (mo.opacity < ALPHA_CUTOFF)
        discard;
    return PC.pickId;
}
#else
uint FragmentMain() : SV_Target
{
    return PC.pickId;
}
#endif
