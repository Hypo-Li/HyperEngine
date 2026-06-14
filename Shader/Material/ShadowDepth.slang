#include "../Common/Binding.slang"

struct ShadowDepthParameters
{
    float depthBias;
    float slopeBias;
};
[[vk::push_constant]]
ShadowDepthParameters PC;

#define WITHOUT_MATERIAL
#ifndef WITHOUT_MATERIAL
[MATERIAL_SOURCE]

[VERTEX_ATTRIBUTE_GETTERS]
#else
struct VSInput
{
    float3 positon : POSITION;
    float3 normal : NORMAL;
    uint instanceID : SV_InstanceID;
    uint startInstanceLocation : SV_StartInstanceLocation;
}

float3 getPosition(in VSInput input)
{
    return input.positon;
}

float3 getNormal(in VSInput input)
{
    return input.normal;
}

uint getInstanceIndex(in VSInput input)
{
    return input.instanceID + input.startInstanceLocation;
}
#endif

[shader("vertex")]
void VertexMain(
    in VSInput input,
    out float4 oPosition : SV_Position
)
{
    uint instanceIndex = sInstanceIndices[getInstanceIndex(input)];
    InstanceData instanceData = sInstanceData[instanceIndex];
    ViewData viewData = sViewData[0];

    float4x4 localToView;
    calculateLocalToView(
        instanceData.localToWorld,
        viewData.worldToViewRot,
        viewData.cameraOrigin,
        localToView
    );
    float3x3 localToViewRot = mul(instanceData.localToWorldRot, viewData.worldToViewRot);

    float4 positionCS = mul(mul(float4(getPosition(input), 1), localToView), viewData.viewToClip);
    positionCS *= 1.0 / positionCS.w;

    float3 lightDirWS = viewData.worldToViewRot[2];
    float3 normalWS = mul(getNormal(input), instanceData.localToWorldRot);
    float NoL = dot(normalWS, lightDirWS);
    constexpr float MaxSlopeDepthBias = 1.0;
    float slope = clamp(abs(NoL) > 0 ? sqrt(saturate(1 - NoL * NoL)) / NoL : MaxSlopeDepthBias, 0.0, MaxSlopeDepthBias);
    float depthBias = PC.depthBias + PC.slopeBias * slope;
    //positionCS.z += depthBias;
    oPosition = positionCS;
}

//#ifndef WITHOUT_MATERIAL
[shader("fragment")]
void FragmentMain()
{
    
}
//#endif