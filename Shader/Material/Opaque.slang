#include "../Common/Binding.slang"

#define BLEND_MODE_OPAQUE 0
#define BLEND_MODE_MASKED 1
#define BLEND_MODE_TRANSLUCENT 2

#ifndef BLEND_MODE
    #define BLEND_MODE BLEND_MODE_OPAQUE
#endif

struct V2F
{
    float3 positionVS;
    float3 normalVS;
    float4 tangentVS;
    float4 color;
    float2 texCoord0;
};

struct MaterialInput
{
    float3 normalWS;
    float4 tangentWS;
    float4 color;
    float2 texCoord0;
    float3 viewDirWS;
    float time;

    __init(in V2F v2f, in ViewData viewData)
    {
        const float3x3 viewToWorldRot = transpose(viewData.worldToViewRot);
        normalWS = mul(normalize(v2f.normalVS), viewToWorldRot);
        tangentWS = float4(mul(normalize(v2f.tangentVS.xyz), viewToWorldRot), v2f.tangentVS.w);
        color = v2f.color;
        texCoord0 = v2f.texCoord0;
        viewDirWS = mul(float3(0, 0, -1), viewData.worldToViewRot);
        time = uFrameData.wrappedTime;
    }
};

struct MaterialOutput
{
    float3 emissive;
    float3 baseColor;
    float metallic;
    float roughness;
    float specular;
    float3 normal;
    float occlusion;
    float opacity;

    __init()
    {
        emissive = float3(0);
        baseColor = float3(1);
        metallic = 0;
        roughness = 0.5;
        specular = 0.5;
        normal = float3(0, 0, 1);
        occlusion = 1;
        opacity = 1;
    }
};

[MATERIAL_SOURCE]

[VERTEX_ATTRIBUTE_GETTERS]

[shader("vertex")]
void VertexMain(
    in VSInput input,
    out V2F v2f,
    out float4 oPosition : SV_Position
)
{
    uint instanceIndex = sInstanceIndices[getInstanceIndex(input)];
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

    v2f.positionVS = mul(float4(getPosition(input), 1), localToView).xyz;
    v2f.normalVS = mul(getNormal(input), localToViewRot);
    const float4 tangent = getTangent(input);
    v2f.tangentVS = float4(mul(tangent.xyz, localToViewRot), tangent.w);
    v2f.color = getColor(input);
    v2f.texCoord0 = getTexCoord0(input);

    const float4 positionCS = mul(float4(v2f.positionVS, 1), viewData.viewToClip);
    oPosition = positionCS;
}

struct FSOutput
{
    float4 sceneColor   : SV_Target0;
    float4 gbufferA     : SV_Target1;
    float4 gbufferB     : SV_Target2;
    float4 gbufferC     : SV_Target3;
};

[shader("fragment")]
void FragmentMain(
    in V2F v2f,
    out FSOutput output
)
{
    MaterialInput mi = MaterialInput(v2f, sViewData[0]);
    MaterialOutput mo;
    evaluateMaterial(mi, mo);

    const float3 bitangentWS = normalize(cross(mi.normalWS, mi.tangentWS.xyz)) * mi.tangentWS.w;
    const float3x3 tangentToWorld = float3x3(mi.tangentWS.xyz, bitangentWS, mi.normalWS);
    const float3 normalWS = mul(mo.normal, tangentToWorld);

    output.sceneColor = float4(mo.emissive, 1.0);
    output.gbufferA = float4(normalWS * 0.5 + 0.5, 1.0);
    output.gbufferB = float4(mo.metallic, mo.roughness, mo.specular,  0.0);
    output.gbufferC = float4(mo.baseColor, mo.occlusion);
}