#include <Common/Defines.slang>

#ifndef MATERIAL_DOMAIN
    #define MATERIAL_DOMAIN MATERIAL_DOMAIN_SKY
#endif

#include <Common/Binding.slang>
#include <Common/VertexPositionOffset.slang>
#include <Common/MaterialIO.slang>
#include <Common/Cubemap.slang>

// 和 SkyBackgroundPass.cpp 里的 SkyBackgroundPushConstants 逐字节一致, 总共 64 字节(rhi::kImmediateDataSize)
struct SkyBackgroundPushConstants
{
    float3 mainLight0Direction;
    float intensity;
    float3 mainLight1Direction;
    int captureFace;
    float3 mainLight0Intensity;
    float captureFaceSize;
    float3 mainLight1Intensity;
    float padding;
};
[[vk::push_constant]]
SkyBackgroundPushConstants PC;

[MATERIAL_IMPLEMENTATION]

static constexpr float2 kPositions[3] = {
    float2(-1, -3),
    float2( 3,  1),
    float2(-1,  1)
};

[shader("vertex")]
float4 VertexMain(in uint vertexID : SV_VertexID) : SV_Position
{
    return float4(kPositions[vertexID], 0, 1);
}

[shader("fragment")]
float4 FragmentMain(in float4 svPosition : SV_Position) : SV_Target
{
    float3 rayDirWS;
    float preExposure = 1.0;
    if (PC.captureFace < 0)
    {
        float2 uv = svPosition.xy / uViewData.resolution.xy;
        float4 clipSpace = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, 0.5, 1.0);
        float4 viewSpace = mul(clipSpace, uViewData.clipToView);
        viewSpace *= 1.0 / viewSpace.w;
        rayDirWS = normalize(mul(uViewData.worldToViewRot, viewSpace.xyz));
        preExposure = getPreExposure(uViewData);
    }
    else
    {
        rayDirWS = faceUVToCubemapDir(uint(PC.captureFace), svPosition.xy / PC.captureFaceSize);
    }

    MaterialInput mi;
    mi.viewDirWS = -rayDirWS;
    mi.time = uFrameData.wrappedTime;
    mi.mainLights[0].direction = PC.mainLight0Direction;
    mi.mainLights[0].intensity = PC.mainLight0Intensity;
    mi.mainLights[1].direction = PC.mainLight1Direction;
    mi.mainLights[1].intensity = PC.mainLight1Intensity;

    MaterialOutput mo = MaterialOutput();
    evaluateMaterial(mi, mo);
    return float4(mo.emissive * PC.intensity * preExposure, 1.0);
}
