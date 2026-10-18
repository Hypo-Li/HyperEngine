// 统一屏幕探针网格降采样: 每个探针对应一个固定像素间距的 tile, 在这一帧共享的时域抖动位置上
// 采一次 GBuffer, 把线性深度(符号编码有效性)/世界空间法线/世界空间速度写进探针图集。
// 跟 UE ScreenProbeDownsampleDepthUniformCS(LumenScreenProbeGather.usf:117-131)对应, 见
// Shader/ScreenProbe/ScreenProbeCommon.slang 顶部的偏差说明。
#include "../Common/Binding.slang"
#include "../Common/BindingAttributes.slang"
#include "../Common/GBufferEncoding.slang"
#include "ScreenProbeCommon.slang"

[[vk::binding(0, 2)]]
Texture2D<uint2> GbufferAImage;

[[vk::binding(1, 2)]]
Texture2D<float4> GbufferBImage;

[[vk::binding(2, 2)]]
DepthTexture2D SceneDepthImage;

[[vk::binding(3, 2)]]
[format("r32f")]
RWTexture2D<float> ScreenProbeSceneDepth;

[[vk::binding(4, 2)]]
[format("rg8")]
RWTexture2D<float2> ScreenProbeWorldNormal;

[[vk::binding(5, 2)]]
[UnfilterableFloat]
Texture2D<float4> VelocityImage;

[[vk::binding(6, 2)]]
[format("r16f")]
RWTexture2D<float> ScreenProbeWorldSpeed;

struct ScreenProbeDownsampleUniformPushConstants
{
    uint2 viewRectMin;
    uint2 viewExtent;
    uint2 tileGridSize;
};
[[vk::push_constant]]
ScreenProbeDownsampleUniformPushConstants PC;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 tileCoord = dispatchThreadID.xy;
    if (any(tileCoord >= PC.tileGridSize))
        return;

    uint2 screenPosition = getClampedUniformScreenProbeScreenPosition(tileCoord, PC.viewRectMin, PC.viewExtent, screenProbeFrameIndex());

    float deviceZ = SceneDepthImage[screenPosition].r;
    bool valid = deviceZ != 0.0;

    uint2 gbufferA = GbufferAImage[screenPosition];
    float4 gbufferB = GbufferBImage[screenPosition];
    ShadingModel shadingModel;
    bool hasAnisotropy;
    unpackShadingModelByte(gbufferB.w, shadingModel, hasAnisotropy);
    valid = valid && (shadingModel != ShadingModel::Unlit);

    float3 normalWS = decodeGBufferNormal(gbufferA.r);

    // <=0 表示无效(天空/Unlit), 见 ScreenProbeCommon.slang::calculateUniformUpsampleWeights
    // 里对这个符号的消费——固定写 -1.0 而不是 -linearDepth, 无效探针的深度数值本身没有意义
    float linearDepth = valid ? screenProbeLinearDepth(deviceZ, uViewData) : -1.0;

    float speed = 0.0;
    if (valid)
    {
        float2 uv = (float2(screenPosition) + 0.5) * uViewData.resolution.zw;
        float3 viewPosition = screenProbeViewPositionAtDepth(uv, linearDepth, uViewData);
        speed = calculateWorldSpaceSpeed(viewPosition, VelocityImage[screenPosition], uViewData);
    }

    ScreenProbeSceneDepth[tileCoord] = linearDepth;
    ScreenProbeWorldNormal[tileCoord] = octEncode(normalWS);
    ScreenProbeWorldSpeed[tileCoord] = speed;
}
