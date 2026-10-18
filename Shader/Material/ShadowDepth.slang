#define QUAD_VIEW_DEPENDENT_ADJUST 0

#include <Common/Binding.slang>
#include <Common/VertexPositionOffset.slang>
#include <Common/MaterialIO.slang>

[[vk::binding(0, 3)]]
StructuredBuffer<InstanceData> sInstanceData;

[MATERIAL_IMPLEMENTATION]

[VERTEX_LAYOUT_IMPLEMENTATION]

// 和 CascadeShadowDepthPass.cpp 里 CascadeShadowData::depthBias/slopeDepthBias 的打包顺序对应,
// 已经是能直接加到 [0,1] NDC 深度上的值(在 C++ 侧已经除过这一级 cascade 的深度范围), 不需要
// 在这里再做任何归一化
struct ShadowDepthBiasParams
{
    float depthBias;
    float slopeDepthBias;
};
[[vk::push_constant]]
ShadowDepthBiasParams PC;

// MaterialProxy::usesSharedGeometryShader() 只把 Opaque 且无 VPO 的材质重定向到跨材质共享的
// shader(那份共享 shader 用空宏编译, BLEND_MODE 落回 Defines.slang 里的默认值 BLEND_MODE_OPAQUE)——Masked
// 材质从不满足这个条件, 走的一直是这个文件自己按材质编译出来的独立 shader, 只是过去 FragmentMain
// 从来没调用过 evaluateMaterial() 去做 alpha cutout。这里新增的 VSOuput/MaterialInput 拼装/
// discard 全部包在 BLEND_MODE == BLEND_MODE_MASKED 里, 共享路径(BLEND_MODE 恒为默认值)完全
// 编译不到这段代码, 不受影响。
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

// 阴影 pass 不做光照, 只用 evaluateMaterial() 算 mo.opacity 做 alpha cutout——法线/切线/
// 视线方向这些跟光照相关的字段没有意义, 填占位值即可
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
    mi.time = uFrameData.wrappedTime;
    mi.relativeWorldToObject = calculateRelativeWorldToObject(
        sInstanceData[instanceIndex].worldToObjectRS,
        sInstanceData[instanceIndex].objectToWorld.getTranslation(),
        uViewData.cameraOrigin
    );
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

    // 顶点位置偏移(vertex position offset)按世界空间方向求值, 和 Opaque.slang 的 VertexMain
    // 用同一套约定, 具体语义见 VertexPositionOffset.slang 的注释
    VertexPositionOffsetInput vpoi;
    vpoi.positionOS = geo.position;
    vpoi.normalOS = geo.normal;
    vpoi.time = uFrameData.wrappedTime;
    vpoi.objectToWorld = instanceData.objectToWorld;
    positionVS += mul(evaluateVertexPositionOffset(vpoi), uViewData.worldToViewRot);

    float4 positionCS = mul(float4(positionVS, 1), uViewData.viewToClip);
    positionCS *= 1.0 / positionCS.w;

    // worldToViewRot 上传的是视图旋转矩阵的转置, 光照方向是它的第 2 列而不是第 2 行。
    float3 lightDirWS = float3(
        uViewData.worldToViewRot[0].z,
        uViewData.worldToViewRot[1].z,
        uViewData.worldToViewRot[2].z
    );
    float3 normalWS = normalize(mul(geo.normal, instanceData.objectToWorldNormalRot));
    // Slope bias is orientation-independent. A back-facing/double-sided triangle has a negative
    // dot product but the same geometric slope as its front-facing counterpart.
    float NoL = abs(dot(normalWS, lightDirWS));
    constexpr float MaxSlopeDepthBias = 1.0;
    float slope = clamp(abs(NoL) > 0 ? sqrt(saturate(1 - NoL * NoL)) / NoL : MaxSlopeDepthBias, 0.0, MaxSlopeDepthBias);
    float depthBias = PC.depthBias + PC.slopeDepthBias * slope;
    // Upstream casters are kept from near-clipping by hardware depth clamp (CascadeShadowDepthPass.cpp), not here.
    positionCS.z += depthBias;

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
void FragmentMain(in VSOuput vsout, nointerpolation uint instanceIndex : TEXCOORD6)
{
    MaterialInput mi = makeMaterialInput(vsout, instanceIndex);
    MaterialOutput mo;
    evaluateMaterial(mi, mo);
    if (mo.opacity < ALPHA_CUTOFF)
        discard;
}
#else
void FragmentMain()
{

}
#endif
