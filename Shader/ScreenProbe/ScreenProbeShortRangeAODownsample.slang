#include "../Common/Binding.slang"
#include "../Common/GBufferEncoding.slang"
#include "ScreenProbeShortRangeAOCommon.slang"

[[vk::binding(0, 2)]]
DepthTexture2D SceneDepthImage;

[[vk::binding(1, 2)]]
Texture2D<uint2> GbufferAImage;

[[vk::binding(2, 2)]]
Texture2D<float4> GbufferBImage;

[[vk::binding(3, 2)]]
[format("r32f")]
RWTexture2D<float> DownsampledSceneDepth;

[[vk::binding(4, 2)]]
[format("rgba8")]
RWTexture2D<float4> DownsampledWorldNormal;

struct ScreenProbeShortRangeAODownsamplePushConstants
{
    uint2 viewRectMin;
    uint2 downsampledViewExtent;
};
[[vk::push_constant]]
ScreenProbeShortRangeAODownsamplePushConstants PC;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 halfResCoord = dispatchThreadID.xy;
    if (any(halfResCoord >= PC.downsampledViewExtent))
        return;

    uint2 downsampledGlobalCoord = halfResCoord + PC.viewRectMin / kShortRangeAODownsampleFactor;
    uint2 jitter = getShortRangeAODownsampleJitter(downsampledGlobalCoord, screenProbeFrameIndex());
    uint2 fullResCoord = downsampledGlobalCoord * kShortRangeAODownsampleFactor + jitter;

    float deviceZ = SceneDepthImage[fullResCoord].r;
    ShadingModel shadingModel;
    bool hasAnisotropy;
    unpackShadingModelByte(GbufferBImage[fullResCoord].w, shadingModel, hasAnisotropy);
    bool valid = deviceZ != 0.0 && shadingModel != ShadingModel::Unlit;

    if (valid)
    {
        float linearDepth = screenProbeLinearDepth(deviceZ, uViewData);
        float3 normalWS = decodeGBufferNormal(GbufferAImage[fullResCoord].r);
        DownsampledSceneDepth[halfResCoord] = linearDepth;
        DownsampledWorldNormal[halfResCoord] = float4(normalWS * 0.5 + 0.5, 1.0);
    }
    else
    {
        DownsampledSceneDepth[halfResCoord] = -1.0;
        DownsampledWorldNormal[halfResCoord] = float4(0.5, 0.5, 1.0, 1.0);
    }
}
