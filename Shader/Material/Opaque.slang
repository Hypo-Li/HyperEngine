#include <Common/Binding.slang>
#include <Common/VertexPositionOffset.slang>
#include <Common/MaterialIO.slang>
#include <Common/GBufferEncoding.slang>

[[vk::binding(0, 3)]]
StructuredBuffer<InstanceData> sInstanceData;

[VERTEX_LAYOUT_IMPLEMENTATION]

struct VSOuput
{
    float4 svPosition : SV_Position;
    float3 positionVS : TEXCOORD0;
    float4 currPositionCS : TEXCOORD1;
    float4 prevPositionCS : TEXCOORD2;
    float3 normalVS : TEXCOORD3;
    float4 tangentVS : TEXCOORD4;
#if VERTEX_HAS_COLOR
    float4 color : TEXCOORD5;
#endif
#if VERTEX_HAS_TEXCOORD0
    float2 texCoord0 : TEXCOORD6;
#endif
#if VERTEX_HAS_TEXCOORD1
    float2 texCoord1 : TEXCOORD7;
#endif
#if VERTEX_HAS_TEXCOORD2
    float2 texCoord2 : TEXCOORD8;
#endif
#if VERTEX_HAS_TEXCOORD3
    float2 texCoord3 : TEXCOORD9;
#endif
};

// 片元入口要把 flat 成员单独声明成参数: 放进入口接收的结构里, Slang 输出 WGSL 时会在那份结构拷贝上
// 残留没有 location 的 @interpolate(flat), 被 WebGPU 拒绝
struct VSStageOutput
{
    VSOuput varyings;
    nointerpolation uint instanceIndex : TEXCOORD10;
};

MaterialInput makeMaterialInput(in VSOuput vsout, in uint instanceIndex, in ViewData viewData, in bool isFrontFace)
{
    MaterialInput mi;
    mi.normalWS = mul(viewData.worldToViewRot, normalize(vsout.normalVS));
    mi.tangentWS = float4(mul(viewData.worldToViewRot, normalize(vsout.tangentVS.xyz)), vsout.tangentVS.w);
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

    mi.viewDirWS = mul(viewData.worldToViewRot, normalize(-vsout.positionVS));
    mi.time = uFrameData.wrappedTime;
    mi.positionRWS = float4(mul(viewData.worldToViewRot, vsout.positionVS), -vsout.positionVS.z);
    mi.relativeWorldToObject = calculateRelativeWorldToObject(
        sInstanceData[instanceIndex].worldToObjectRS,
        sInstanceData[instanceIndex].objectToWorld.getTranslation(),
        uViewData.cameraOrigin
    );
    mi.twoSidedSign = isFrontFace ? 1.0 : -1.0;
    return mi;
}

[MATERIAL_IMPLEMENTATION]

[shader("vertex")]
VSStageOutput VertexMain(in VSInput input)
{
    uint instanceIndex = getInstanceIndex(input);
    const InstanceData instanceData = sInstanceData[instanceIndex];
    const VertexGeometry geo = getVertexGeometry(input);

    float4x4 objectToView;
    calculateObjectToView(
        instanceData.objectToWorld,
        uViewData.worldToViewRot,
        uViewData.cameraOrigin,
        objectToView
    );
    // 切线是贴在表面上、会跟着表面一起形变的向量, 用 R*S 直接变换; 法线要保持垂直关系,
    // 非均匀缩放下必须用 inverse(transpose(RS)), 不能跟切线共用同一个矩阵——两个字段的
    // 区别见 Binding.slang 里 InstanceData 的注释
    const float3x3 objectToViewRS = mul(instanceData.objectToWorldRS, uViewData.worldToViewRot);
    const float3x3 objectToViewNormalRot = mul(instanceData.objectToWorldNormalRot, uViewData.worldToViewRot);

    VSOuput output;
    output.positionVS = mul(float4(geo.position, 1), objectToView).xyz;

#if HAS_VPO
    VertexPositionOffsetInput vpoi;
    vpoi.positionOS = geo.position;
    vpoi.normalOS = geo.normal;
    vpoi.time = uFrameData.wrappedTime;
    vpoi.objectToWorld = instanceData.objectToWorld;
    float3 vertexPositionOffsetWS = evaluateVertexPositionOffset(vpoi);
    output.positionVS += mul(vertexPositionOffsetWS, uViewData.worldToViewRot);
#endif

    output.normalVS = mul(geo.normal, objectToViewNormalRot);
    output.tangentVS = float4(mul(geo.tangent.xyz, objectToViewRS), geo.tangent.w);
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
    output.svPosition = mul(float4(output.positionVS, 1), uViewData.viewToClip);
    output.currPositionCS = mul(float4(output.positionVS, 1), uViewData.viewToClipUnjittered);

    float4x4 prevObjectToView;
    calculateObjectToView(
        instanceData.prevObjectToWorld,
        uViewData.prevWorldToViewRot,
        uViewData.prevCameraOrigin,
        prevObjectToView
    );
    // 蒙皮网格的局部姿势本身也会随时间变化, 不能只用当前姿势的 geo.position 配上一帧的
    // objectToWorld——getPrevPosition() 对蒙皮网格用上一帧的骨骼矩阵重算局部位置, 非蒙皮
    // 网格局部位置从不随时间变化, 直接等于 geo.position
    float3 prevPositionVS = mul(float4(getPrevPosition(input), 1), prevObjectToView).xyz;

#if TIME_DEPENDENT_VPO
    VertexPositionOffsetInput prevVpoi;
    prevVpoi.positionOS = geo.position;
    prevVpoi.normalOS = geo.normal;
    prevVpoi.time = uFrameData.prevWrappedTime;
    prevVpoi.objectToWorld = instanceData.prevObjectToWorld;
    prevPositionVS += mul(evaluateVertexPositionOffset(prevVpoi), uViewData.prevWorldToViewRot);
#elif HAS_VPO
    // 不依赖 time 时直接复用当前帧偏移, 换 prevWorldToViewRot 重新投影, 省掉一次函数求值
    prevPositionVS += mul(vertexPositionOffsetWS, uViewData.prevWorldToViewRot);
#endif

    output.prevPositionCS = mul(float4(prevPositionVS, 1), uViewData.prevViewToClipUnjittered);

    VSStageOutput stageOutput;
    stageOutput.varyings = output;
    stageOutput.instanceIndex = instanceIndex;
    return stageOutput;
}

struct FSOutput
{
    float4 sceneColor   : SV_Target0;
    uint2  gbufferA     : SV_Target1;
    float4 gbufferB     : SV_Target2;
    float4 gbufferC     : SV_Target3;
    float4 gbufferD     : SV_Target4;
    float4 velocity     : SV_Target5;
};

[shader("fragment")]
FSOutput FragmentMain(in VSOuput vsout, nointerpolation uint instanceIndex : TEXCOORD10, bool isFrontFace : SV_IsFrontFace)
{
    MaterialInput mi = makeMaterialInput(vsout, instanceIndex, uViewData, isFrontFace);
    MaterialOutput mo;
    evaluateMaterial(mi, mo);

#if BLEND_MODE == BLEND_MODE_MASKED
    if (mo.opacity < ALPHA_CUTOFF)
        discard;
#endif

    const float3 bitangentWS = normalize(cross(mi.normalWS, mi.tangentWS.xyz)) * mi.tangentWS.w;
    const float3x3 tangentToWorld = float3x3(mi.tangentWS.xyz, bitangentWS, mi.normalWS);
#if TANGENT_SPACE_NORMAL
    const float3 normalWS = mul(mo.normal, tangentToWorld);
#else
    const float3 normalWS = mo.normal;
#endif

    FSOutput output;
    output.sceneColor = float4(mo.emissive * getPreExposure(uViewData), 1.0);

#if IS_ANISOTROPIC
    #if TANGENT_SPACE_NORMAL
        float3 anisotropyTangentWS = mul(mo.anisotropyTangent, tangentToWorld);
    #else
        float3 anisotropyTangentWS = mo.anisotropyTangent;
    #endif
    float anisotropyStrength = mo.anisotropy;
#else
    float3 anisotropyTangentWS = tangentToWorld[0];
    float anisotropyStrength = 0.0;
#endif
    output.gbufferA = uint2(
        encodeGBufferNormal(normalWS),
        encodeAnisotropy(anisotropyTangentWS, normalWS, anisotropyStrength)
    );

#if SHADING_MODEL == SHADING_MODEL_FROM_MATERIAL
    output.gbufferB = float4(
        mo.metallic, mo.roughness, mo.specular,
        packShadingModelByte(mo.shadingModel, IS_ANISOTROPIC != 0) / 255.0
    );
#else
    // 编译期已知的固定值, 直接传常量, 不读 mo.shadingModel(该字段在这个分支下压根不存在)
    output.gbufferB = float4(
        mo.metallic, mo.roughness, mo.specular,
        packShadingModelByte(ShadingModel(SHADING_MODEL), IS_ANISOTROPIC != 0) / 255.0
    );
#endif
    output.gbufferC = float4(mo.baseColor, mo.occlusion);

#if SHADING_MODEL == SHADING_MODEL_FROM_MATERIAL
    if (mo.shadingModel == ShadingModel::Clear_Coat)
    {
    #if TANGENT_SPACE_NORMAL
        float3 clearCoatNormalWS = mul(mo.clearCoatNormal, tangentToWorld);
    #else
        float3 clearCoatNormalWS = mo.clearCoatNormal;
    #endif
        output.gbufferD = float4(mo.clearCoat, mo.clearCoatRoughness,
            encodeClearCoatNormalDelta(clearCoatNormalWS, normalWS));
    }
    else if (mo.shadingModel == ShadingModel::Cloth)
    {
        output.gbufferD = float4(sqrt(saturate(mo.fuzzColor)), saturate(mo.clothAmount));
    }
    else
    {
        output.gbufferD = float4(0, 0, 0, 0);
    }
#elif SHADING_MODEL == SHADING_MODEL_CLEAR_COAT
    // 编译期已知是 Clear_Coat, 不需要运行期判断, 也不需要 else 分支
    #if TANGENT_SPACE_NORMAL
        float3 clearCoatNormalWS = mul(mo.clearCoatNormal, tangentToWorld);
    #else
        float3 clearCoatNormalWS = mo.clearCoatNormal;
    #endif
    output.gbufferD = float4(mo.clearCoat, mo.clearCoatRoughness,
        encodeClearCoatNormalDelta(clearCoatNormalWS, normalWS));
#elif SHADING_MODEL == SHADING_MODEL_CLOTH
    // sqrt 编码提升暗部精度, 跟 UE EncodeSubsurfaceColor 一致, 解码端对应 Square()
    output.gbufferD = float4(sqrt(saturate(mo.fuzzColor)), saturate(mo.clothAmount));
#else
    output.gbufferD = float4(0, 0, 0, 0);
#endif

    // 先在 clip 空间求差再除 w, 两帧输入相同时结果精确为 0
    float rcpCurrW = 1.0 / vsout.currPositionCS.w;
    float rcpPrevW = 1.0 / vsout.prevPositionCS.w;
    float3 velocity = (vsout.currPositionCS.xyz - vsout.prevPositionCS.xyz) * rcpCurrW
        + vsout.prevPositionCS.xyz * (rcpCurrW - rcpPrevW);
    output.velocity = encodeVelocity(velocity);
    return output;
}
