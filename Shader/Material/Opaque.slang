#include <Common/Binding.slang>
#include <Common/VertexPositionOffset.slang>
#include <Common/MaterialIO.slang>

#define BLEND_MODE_OPAQUE 0
#define BLEND_MODE_MASKED 1
#define BLEND_MODE_TRANSLUCENT 2

#ifndef BLEND_MODE
    #define BLEND_MODE BLEND_MODE_OPAQUE
#endif

#ifndef TEXCOORD_COUNT
    #define TEXCOORD_COUNT 1
#endif

#ifndef HAS_VERTEX_COLOR
    #define HAS_VERTEX_COLOR 1
#endif

struct VSOuput
{
    float4 position : SV_Position;
    float3 positionVS : TEXCOORD0;
    float3 normalVS : TEXCOORD1;
    float4 tangentVS : TEXCOORD2;
#if HAS_VERTEX_COLOR
    float4 color : TEXCOORD3;
#endif
#if TEXCOORD_COUNT > 0
    float2 texCoord0 : TEXCOORD4;
#endif
#if TEXCOORD_COUNT > 1
    float2 texCoord1 : TEXCOORD5;
#endif
#if TEXCOORD_COUNT > 2
    float2 texCoord2 : TEXCOORD6;
#endif
#if TEXCOORD_COUNT > 3
    float2 texCoord3 : TEXCOORD7;
#endif
#if TEXCOORD_COUNT > 4
    float2 texCoord4 : TEXCOORD8;
#endif
#if TEXCOORD_COUNT > 5
    float2 texCoord5 : TEXCOORD9;
#endif
#if TEXCOORD_COUNT > 6
    float2 texCoord6 : TEXCOORD10;
#endif
#if TEXCOORD_COUNT > 7
    float2 texCoord7 : TEXCOORD11;
#endif
};

MaterialInput makeMaterialInput(in VSOuput vsout, in ViewData viewData)
{
    MaterialInput mi;
    const float3x3 viewToWorldRot = transpose(viewData.worldToViewRot);
    mi.normalWS = mul(normalize(vsout.normalVS), viewToWorldRot);
    mi.tangentWS = float4(mul(normalize(vsout.tangentVS.xyz), viewToWorldRot), vsout.tangentVS.w);
#if HAS_VERTEX_COLOR
    mi.color = vsout.color;
#else
    mi.color = float4(1);
#endif

#if TEXCOORD_COUNT > 0
    mi.texCoords[0] = vsout.texCoord0;
#else
    mi.texCoords[0] = float2(0, 0);
#endif
#define LAST_TEXCOORD_INDEX 0

#if TEXCOORD_COUNT > 1
    mi.texCoords[1] = vsout.texCoord1;
#undef LAST_TEXCOORD_INDEX
#define LAST_TEXCOORD_INDEX 1
#else
    mi.texCoords[1] = mi.texCoords[LAST_TEXCOORD_INDEX];
#endif

#if TEXCOORD_COUNT > 2
    mi.texCoords[2] = vsout.texCoord2;
#undef LAST_TEXCOORD_INDEX
#define LAST_TEXCOORD_INDEX 2
#else
    mi.texCoords[2] = mi.texCoords[LAST_TEXCOORD_INDEX];
#endif

#if TEXCOORD_COUNT > 3
    mi.texCoords[3] = vsout.texCoord3;
#undef LAST_TEXCOORD_INDEX
#define LAST_TEXCOORD_INDEX 3
#else
    mi.texCoords[3] = mi.texCoords[LAST_TEXCOORD_INDEX];
#endif

#if TEXCOORD_COUNT > 4
    mi.texCoords[4] = vsout.texCoord4;
#undef LAST_TEXCOORD_INDEX
#define LAST_TEXCOORD_INDEX 4
#else
    mi.texCoords[4] = mi.texCoords[LAST_TEXCOORD_INDEX];
#endif

#if TEXCOORD_COUNT > 5
    mi.texCoords[5] = vsout.texCoord5;
#undef LAST_TEXCOORD_INDEX
#define LAST_TEXCOORD_INDEX 5
#else
    mi.texCoords[5] = mi.texCoords[LAST_TEXCOORD_INDEX];
#endif

#if TEXCOORD_COUNT > 6
    mi.texCoords[6] = vsout.texCoord6;
#undef LAST_TEXCOORD_INDEX
#define LAST_TEXCOORD_INDEX 6
#else
    mi.texCoords[6] = mi.texCoords[LAST_TEXCOORD_INDEX];
#endif

#if TEXCOORD_COUNT > 7
    mi.texCoords[7] = vsout.texCoord7;
#undef LAST_TEXCOORD_INDEX
#define LAST_TEXCOORD_INDEX 7
#else
    mi.texCoords[7] = mi.texCoords[LAST_TEXCOORD_INDEX];
#endif

    mi.viewDirWS = mul(viewData.worldToViewRot, normalize(-vsout.positionVS));
    mi.time = uFrameData.wrappedTime;
    return mi;
}

[MATERIAL_IMPLEMENTATION]

[VERTEX_ATTRIBUTE_GETTERS]

[shader("vertex")]
VSOuput VertexMain(in VSInput input)
{
    uint instanceIndex = sIndices[getInstanceIndex(input)];
    const InstanceData instanceData = sInstanceData[instanceIndex];
    const ViewData viewData = sViewData[0];

    float4x4 localToView;
    calculateLocalToView(
        instanceData.localToWorld,
        viewData.worldToViewRot,
        viewData.cameraOrigin,
        localToView
    );
    const float3x3 localToViewRot = mul(instanceData.localToWorldRot, viewData.worldToViewRot);

    VSOuput output;
    output.positionVS = mul(float4(getPosition(input), 1), localToView).xyz;
    
    VertexPositionOffsetInput vpoi;
    vpoi.positionLS = getPosition(input);
    vpoi.normalLS = getNormal(input);
    vpoi.time = uFrameData.wrappedTime;
    vpoi.localToWorld = instanceData.localToWorld;
    output.positionVS += mul(evaluateVertexPositionOffset(vpoi), viewData.worldToViewRot);

    output.normalVS = mul(getNormal(input), localToViewRot);
    const float4 tangent = getTangent(input);
    output.tangentVS = float4(mul(tangent.xyz, localToViewRot), tangent.w);
#if HAS_VERTEX_COLOR
    output.color = getColor(input);
#endif
#if TEXCOORD_COUNT > 0
    output.texCoord0 = getTexCoord0(input);
#endif
#if TEXCOORD_COUNT > 1
    output.texCoord1 = getTexCoord1(input);
#endif
#if TEXCOORD_COUNT > 2
    output.texCoord2 = getTexCoord2(input);
#endif
#if TEXCOORD_COUNT > 3
    output.texCoord3 = getTexCoord3(input);
#endif
#if TEXCOORD_COUNT > 4
    output.texCoord4 = getTexCoord4(input);
#endif
#if TEXCOORD_COUNT > 5
    output.texCoord5 = getTexCoord5(input);
#endif
#if TEXCOORD_COUNT > 6
    output.texCoord6 = getTexCoord6(input);
#endif
#if TEXCOORD_COUNT > 7
    output.texCoord7 = getTexCoord7(input);
#endif
    output.position = mul(float4(output.positionVS, 1), viewData.viewToClip);
    return output;
}

struct FSOutput
{
    float4 sceneColor   : SV_Target0;
    float4 gbufferA     : SV_Target1;
    float4 gbufferB     : SV_Target2;
    float4 gbufferC     : SV_Target3;
};

[shader("fragment")]
FSOutput FragmentMain(in VSOuput vsout)
{
    MaterialInput mi = makeMaterialInput(vsout, sViewData[0]);
    MaterialOutput mo;
    evaluateMaterial(mi, mo);

    const float3 bitangentWS = normalize(cross(mi.normalWS, mi.tangentWS.xyz)) * mi.tangentWS.w;
    const float3x3 tangentToWorld = float3x3(mi.tangentWS.xyz, bitangentWS, mi.normalWS);
    const float3 normalWS = mul(mo.normal, tangentToWorld);

    FSOutput output;
    output.sceneColor = float4(mo.emissive, 1.0);
    output.gbufferA = float4(normalWS * 0.5 + 0.5, 1.0);
    output.gbufferB = float4(mo.metallic, mo.roughness, mo.specular,  0.0);
    output.gbufferC = float4(mo.baseColor, mo.occlusion);
    return output;
}