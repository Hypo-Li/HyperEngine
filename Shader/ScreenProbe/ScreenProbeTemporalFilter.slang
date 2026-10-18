// 对应 UE ScreenProbeTemporalReprojectionCS 的核心路径, 简化掉 FastUpdateMode/Substrate(LumenScreenProbeGatherTemporal.usf:289-580), 见 Document/Lumen_ScreenProbeGather_Plan.md §6.8。
// RoughSpecularIndirect 时域滤波跟漫反射共用同一份重投影权重和 alpha, 见 §6.8c。
#include "../Common/Binding.slang"
#include "../Common/BindingAttributes.slang"
#include "../Common/GBufferEncoding.slang"
#include "ScreenProbeCommon.slang"
#include "ScreenProbeShortRangeAOCommon.slang"

[[vk::binding(0, 2)]]
DepthTexture2D SceneDepth;

[[vk::binding(1, 2)]]
Texture2D<uint2> GbufferAImage;

[[vk::binding(2, 2)]]
Texture2D<float4> GbufferBImage;

[[vk::binding(3, 2)]]
[UnfilterableFloat]
Texture2D<float4> Velocity;

[[vk::binding(4, 2)]]
Texture2D<float> SceneDepthHistory;

[[vk::binding(5, 2)]]
Texture2D<float4> DiffuseIndirectHistory;

[[vk::binding(6, 2)]]
Texture2D<float4> DiffuseIndirect;

[[vk::binding(7, 2)]]
SamplerState PointClampSampler;

[[vk::binding(8, 2)]]
[format("rgba16f")]
RWTexture2D<float4> DiffuseIndirectFiltered;

[[vk::binding(9, 2)]]
[format("rgba16f")]
RWTexture2D<float4> NewDiffuseIndirectHistory;

[[vk::binding(10, 2)]]
Texture2D<float> LightingValid;

[[vk::binding(11, 2)]]
// 实际纹理是 R8_UINT, 不是 Slang 默认打的 R32ui, 跟 TAA.slang 的 SceneStencil 同一个坑
[format("unknown")]
Texture2D<uint> ShortRangeAOStochasticOffset;

[[vk::binding(12, 2)]]
Texture2D<uint> ShortRangeAO;

[[vk::binding(13, 2)]]
Texture2D<uint> ShortRangeAOHistory;

[[vk::binding(14, 2)]]
[format("r32ui")]
RWTexture2D<uint> NewShortRangeAOHistory;

// 粗糙镜面(R6 RoughSpecularIndirect)时域滤波——跟漫反射共用同一份 finalWeights/totalWeight/alpha
// (UE ScreenProbeTemporalReprojectionCS 同样两个 channel 共享一次重投影结果), 不单独算
[[vk::binding(15, 2)]]
Texture2D<float4> RoughSpecularIndirect;

[[vk::binding(16, 2)]]
Texture2D<float4> RoughSpecularIndirectHistory;

[[vk::binding(17, 2)]]
[format("rgba16f")]
RWTexture2D<float4> RoughSpecularIndirectFiltered;

[[vk::binding(18, 2)]]
[format("rgba16f")]
RWTexture2D<float4> NewRoughSpecularIndirectHistory;

struct ScreenProbeTemporalFilterPushConstants
{
    uint2 viewRectMin;
    uint historyValid;
    float maxFramesAccumulated;
    uint2 downsampledViewExtent;
};
[[vk::push_constant]]
ScreenProbeTemporalFilterPushConstants PC;

// UE r.Lumen.ScreenProbeGather.Temporal.DistanceThreshold 默认值
static const float kDistanceThreshold = 0.01;

float4 unpackShortRangeAO(uint packedValue)
{
    float3 bentNormal = unpackScreenBentNormal(packedValue);
    float ao = length(bentNormal);
    return float4(bentNormal / max(ao, 0.0001), ao);
}

uint packShortRangeAO(float4 directionAndAO)
{
    float3 direction = directionAndAO.xyz / max(length(directionAndAO.xyz), 0.0001);
    return packScreenBentNormal(direction * directionAndAO.w);
}

float4 loadShortRangeAO(int2 downsampledGlobalCoord)
{
    int2 minCoord = int2(PC.viewRectMin / kShortRangeAODownsampleFactor);
    int2 maxCoord = minCoord + int2(PC.downsampledViewExtent) - 1;
    int2 localCoord = clamp(downsampledGlobalCoord, minCoord, maxCoord) - minCoord;
    return unpackShortRangeAO(ShortRangeAO[localCoord]);
}

void getShortRangeAONeighborhood(int2 centerCoord, float4 centerAO, out float4 mean, out float4 extent)
{
    float4 sum = centerAO;
    float4 squareSum = centerAO * centerAO;
    for (int offsetY = -1; offsetY <= 1; ++offsetY)
    {
        for (int offsetX = -1; offsetX <= 1; ++offsetX)
        {
            if (offsetX == 0 && offsetY == 0)
                continue;
            float4 neighborAO = loadShortRangeAO(centerCoord + int2(offsetX, offsetY));
            sum += neighborAO;
            squareSum += neighborAO * neighborAO;
        }
    }
    mean = sum / 9.0;
    float4 variance = max(squareSum / 9.0 - mean * mean, 0.0);
    extent = kShortRangeAOTemporalNeighborhoodClampScale * sqrt(variance);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint width, height;
    DiffuseIndirectFiltered.GetDimensions(width, height);
    if (dispatchThreadID.x >= width || dispatchThreadID.y >= height)
        return;

    uint2 pixel = dispatchThreadID.xy;
    uint2 screenCoord = pixel + PC.viewRectMin;
    float3 newLighting = DiffuseIndirect[pixel].rgb;
    float3 newRoughSpecular = RoughSpecularIndirect[pixel].rgb;

    uint stochasticPacked = ShortRangeAOStochasticOffset[pixel];
    int2 stochasticOffset = int2(stochasticPacked & 1u, (stochasticPacked >> 1u) & 1u);
    bool validReconstruction = (stochasticPacked & 4u) != 0u;
    int2 aoSampleCoord = shortRangeAODownsampleAnchor(int2(screenCoord)) + stochasticOffset;
    float4 newAO = loadShortRangeAO(aoSampleCoord);
    uint newAOPacked = packShortRangeAO(newAO);

    if (PC.historyValid == 0)
    {
        DiffuseIndirectFiltered[pixel] = float4(newLighting, 1.0);
        NewDiffuseIndirectHistory[pixel] = float4(newLighting, 0.0);
        NewShortRangeAOHistory[pixel] = newAOPacked;
        RoughSpecularIndirectFiltered[pixel] = float4(newRoughSpecular, 1.0);
        NewRoughSpecularIndirectHistory[pixel] = float4(newRoughSpecular, 0.0);
        return;
    }

    float deviceZ = SceneDepth[screenCoord].r;
    ShadingModel shadingModel;
    bool hasAnisotropy;
    unpackShadingModelByte(GbufferBImage[screenCoord].w, shadingModel, hasAnisotropy);
    if (deviceZ == 0.0 || shadingModel == ShadingModel::Unlit)
    {
        DiffuseIndirectFiltered[pixel] = float4(newLighting, 1.0);
        NewDiffuseIndirectHistory[pixel] = float4(newLighting, 0.0);
        NewShortRangeAOHistory[pixel] = getShortRangeAOIdentityPacked();
        RoughSpecularIndirectFiltered[pixel] = float4(newRoughSpecular, 1.0);
        NewRoughSpecularIndirectHistory[pixel] = float4(newRoughSpecular, 0.0);
        return;
    }

    float4 encodedVelocity = Velocity[screenCoord];
    float2 velocityNDC = decodeVelocity(encodedVelocity).xy;
    if (!all(abs(velocityNDC) < float2(4.0)))
    {
        DiffuseIndirectFiltered[pixel] = float4(newLighting, 1.0);
        NewDiffuseIndirectHistory[pixel] = float4(newLighting, 0.0);
        NewShortRangeAOHistory[pixel] = newAOPacked;
        RoughSpecularIndirectFiltered[pixel] = float4(newRoughSpecular, 1.0);
        NewRoughSpecularIndirectHistory[pixel] = float4(newRoughSpecular, 0.0);
        return;
    }

    float2 currentPixelUV = (float2(screenCoord) + 0.5) * uViewData.resolution.zw;
    float2 velocityUV = velocityNDC * float2(0.5, -0.5);
    float2 historyUV = currentPixelUV - velocityUV;

    float3 outLighting = newLighting;
    float3 outRoughSpecular = newRoughSpecular;
    float4 outAO = newAO;
    float outNumFramesAccumulated = 0.0;

    if (!(any(historyUV < 0.0) || any(historyUV > 1.0)))
    {
        float3 normalWS = decodeGBufferNormal(GbufferAImage[screenCoord].r);
        float3 normalVS = mul(normalWS, uViewData.worldToViewRot);
        float linearDepth = screenProbeLinearDepth(deviceZ, uViewData);
        float3 viewPosition = screenProbeViewRayAtUnitDepth(currentPixelUV, uViewData) * linearDepth;
        float NoV = saturate(dot(normalVS, normalize(-viewPosition)));

        // 跟 UE HistoryScreenPosition.z 对应, 由当前像素 deviceZ 减去速度里的 deviceZ 分量换算得出
        float reprojectedDeviceZ = deviceZ - decodeVelocity(encodedVelocity).z;
        float reprojectedLinearDepth = screenProbeLinearDepth(reprojectedDeviceZ, uViewData);
        float depthThreshold = reprojectedLinearDepth * kDistanceThreshold / lerp(0.1, 1.0, NoV);

        float2 historyTexel = historyUV * float2(width, height) - 0.5;
        float2 historyOrigin = floor(historyTexel);
        float2 bilinearWeights = historyTexel - historyOrigin;
        float2 gatherUV = (historyOrigin + 1.0) * uViewData.resolution.zw;

        float4 historyDeviceZ4 = SceneDepthHistory.GatherRed(PointClampSampler, gatherUV).wzxy;
        float4 historyLinearDepth4 = float4(
            screenProbeLinearDepth(historyDeviceZ4.x, uViewData),
            screenProbeLinearDepth(historyDeviceZ4.y, uViewData),
            screenProbeLinearDepth(historyDeviceZ4.z, uViewData),
            screenProbeLinearDepth(historyDeviceZ4.w, uViewData));
        float4 tapValid = select(abs(historyLinearDepth4 - reprojectedLinearDepth) < depthThreshold, 1.0, 0.0);

        float4 bilinearWeight4 = float4(
            (1.0 - bilinearWeights.x) * (1.0 - bilinearWeights.y),
            bilinearWeights.x * (1.0 - bilinearWeights.y),
            (1.0 - bilinearWeights.x) * bilinearWeights.y,
            bilinearWeights.x * bilinearWeights.y);
        // 跟 UE StochasticLightingTileClassification.usf 一样丢弃双线性权重 <=0.01 的 tap, 否则归一化后它们的权重会被过度放大
        float4 finalWeights = bilinearWeight4 * tapValid * select(bilinearWeight4 > 0.01, 1.0, 0.0);
        float totalWeight = dot(finalWeights, float4(1.0));

        if (totalWeight > 0.0001)
        {
            float4 historyR4 = DiffuseIndirectHistory.GatherRed(PointClampSampler, gatherUV).wzxy;
            float4 historyG4 = DiffuseIndirectHistory.GatherGreen(PointClampSampler, gatherUV).wzxy;
            float4 historyB4 = DiffuseIndirectHistory.GatherBlue(PointClampSampler, gatherUV).wzxy;
            float4 historyA4 = DiffuseIndirectHistory.GatherAlpha(PointClampSampler, gatherUV).wzxy;

            float3 historyLighting = float3(dot(historyR4, finalWeights), dot(historyG4, finalWeights), dot(historyB4, finalWeights)) / totalWeight;
            historyLighting *= getPreExposure(uViewData) / max(uViewData.prevPreExposure, 0.0001);

            float4 historyRoughR4 = RoughSpecularIndirectHistory.GatherRed(PointClampSampler, gatherUV).wzxy;
            float4 historyRoughG4 = RoughSpecularIndirectHistory.GatherGreen(PointClampSampler, gatherUV).wzxy;
            float4 historyRoughB4 = RoughSpecularIndirectHistory.GatherBlue(PointClampSampler, gatherUV).wzxy;
            float3 historyRoughSpecular = float3(dot(historyRoughR4, finalWeights), dot(historyRoughG4, finalWeights), dot(historyRoughB4, finalWeights)) / totalWeight;
            historyRoughSpecular *= getPreExposure(uViewData) / max(uViewData.prevPreExposure, 0.0001);

            int2 historyOriginCoord = int2(historyOrigin);
            int2 historyMaxCoord = int2(width, height) - 1;
            float4 historyAO = (
                unpackShortRangeAO(ShortRangeAOHistory[clamp(historyOriginCoord + int2(0, 0), 0, historyMaxCoord)]) * finalWeights.x
                + unpackShortRangeAO(ShortRangeAOHistory[clamp(historyOriginCoord + int2(1, 0), 0, historyMaxCoord)]) * finalWeights.y
                + unpackShortRangeAO(ShortRangeAOHistory[clamp(historyOriginCoord + int2(0, 1), 0, historyMaxCoord)]) * finalWeights.z
                + unpackShortRangeAO(ShortRangeAOHistory[clamp(historyOriginCoord + int2(1, 1), 0, historyMaxCoord)]) * finalWeights.w
            ) / totalWeight;

            float4 neighborhoodMean, neighborhoodExtent;
            getShortRangeAONeighborhood(aoSampleCoord, newAO, neighborhoodMean, neighborhoodExtent);
            historyAO = clamp(historyAO, neighborhoodMean - neighborhoodExtent, neighborhoodMean + neighborhoodExtent);

            float historyNumFramesAccumulated = dot(historyA4 + 1.0, finalWeights) / totalWeight;
            outNumFramesAccumulated = min(historyNumFramesAccumulated, PC.maxFramesAccumulated);

            float alpha = 1.0 / (1.0 + outNumFramesAccumulated);
            bool lightingIsValid = LightingValid[pixel] > 0.0 && validReconstruction;
            // 当前样本无效时更依赖历史
            if (!lightingIsValid && outNumFramesAccumulated >= 1.0)
                alpha = 1.0 / (1.0 + 4.0 * outNumFramesAccumulated);

            outLighting = lerp(historyLighting, newLighting, alpha);
            outRoughSpecular = lerp(historyRoughSpecular, newRoughSpecular, alpha);
            outAO = lerp(historyAO, newAO, alpha);
        }
    }

    DiffuseIndirectFiltered[pixel] = float4(outLighting, 1.0);
    NewDiffuseIndirectHistory[pixel] = float4(outLighting, outNumFramesAccumulated);
    NewShortRangeAOHistory[pixel] = packShortRangeAO(outAO);
    RoughSpecularIndirectFiltered[pixel] = float4(outRoughSpecular, 1.0);
    NewRoughSpecularIndirectHistory[pixel] = float4(outRoughSpecular, 0.0);
}
