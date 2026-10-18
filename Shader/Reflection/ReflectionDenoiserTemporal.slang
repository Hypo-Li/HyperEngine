// 对应 UE LumenReflectionDenoiserTemporalCS(LRDT.usf):两路历史重投影(表面 + 命中点)、
// YCoCg 邻域钳位(5x5 去 4 角 = 21 taps,直接读, 不搭 groupshared——跟 M9 Short Range AO
// 同一个"跳过 groupshared 缓存"的既有简化)、按置信度混合。LOBE_CONFIDENCE(按瓣形失配加速
// 累积)是 UE #define 0, 不做。见 Document/Lumen_Reflections_Plan.md §3.4
#include "../Common/Binding.slang"
#include "../Common/BindingAttributes.slang"
#include "../Common/GBufferEncoding.slang"
#include "../Common/MaterialIO.slang"
#include "../Common/Math.slang"
#include "../Material/ShadingParams.slang"
#include "ReflectionDenoiserCommon.slang"
#include "ReflectionCommon.slang"

[[vk::binding(0, 2)]]
Texture2D<float4> ResolvedSpecularIndirect;

[[vk::binding(1, 2)]]
Texture2D<float> ResolvedSpecularIndirectDepth;

[[vk::binding(2, 2)]]
Texture2D<float> RayDepth;

[[vk::binding(3, 2)]]
Texture2D<uint2> GbufferAImage;

[[vk::binding(4, 2)]]
Texture2D<float4> GbufferBImage;

[[vk::binding(5, 2)]]
Texture2D<float4> GbufferCImage;

[[vk::binding(6, 2)]]
Texture2D<float4> GbufferDImage;

[[vk::binding(7, 2)]]
[UnfilterableFloat]
Texture2D<float4> Velocity;

[[vk::binding(8, 2)]]
DepthTexture2D SceneDepthImage;

[[vk::binding(9, 2)]]
Texture2D<float> SceneDepthHistory;

[[vk::binding(10, 2)]]
Texture2D<float4> SpecularAndSecondMomentHistory;

[[vk::binding(11, 2)]]
Texture2D<float> NumFramesAccumulatedHistory;

[[vk::binding(12, 2)]]
SamplerState PointClampSampler;

[[vk::binding(13, 2)]]
[format("rgba16f")]
RWTexture2D<float4> NewSpecularAndSecondMoment;

[[vk::binding(14, 2)]]
[format("r8")]
RWTexture2D<float> NewNumFramesAccumulated;

struct ReflectionDenoiserTemporalPushConstants
{
    uint2 viewRectMin;
    uint2 viewExtent;
    uint historyValid;
};
[[vk::push_constant]]
ReflectionDenoiserTemporalPushConstants PC;

// UE r.Lumen.Reflections.Temporal.MaxFramesAccumulated 默认值
static const float kMaxFramesAccumulated = 12.0;

// UE r.Lumen.Reflections.Temporal.DistanceThreshold 默认值
static const float kHistoryDistanceThreshold = 0.03;

// UE r.Lumen.Reflections.Temporal.NeighborhoodClampScale 默认值
static const float kTemporalNeighborhoodClampScale = 1.0;

// 累积帧数打包成 R8_UNORM, 跟 ReflectionDenoiserCommon.slang 的 half 版本分开是因为这里存储格式
// 是 UNORM 不是浮点。numFramesAccumulated 恒不超过 kMaxFramesAccumulated(下面累积公式的
// min(...) 保证), 直接按这个上限线性映射到 [0,1] 不需要额外余量
float packNumFramesAccumulatedUnorm(float numFrames)
{
    return saturate(numFrames / kMaxFramesAccumulated);
}

float unpackNumFramesAccumulatedUnorm(float packedValue)
{
    return packedValue * kMaxFramesAccumulated;
}

struct LightingHistory
{
    bool valid;
    float3 specular;
    float secondMoment;
    float numFramesAccumulated;
};

LightingHistory initInvalidHistory()
{
    LightingHistory h;
    h.valid = false;
    h.specular = float3(0.0);
    h.secondMoment = 0.0;
    h.numFramesAccumulated = 0.0;
    return h;
}

// 4-tap 双线性历史采样, 可选深度测试(表面历史需要, 命中点历史不需要——UE REFLECT_HIT_DEPTH_TEST=0)
LightingHistory sampleHistory(float2 historyUV, bool useDepthTest, float reprojectedLinearDepth, float depthThreshold, in ViewData viewData)
{
    LightingHistory result = initInvalidHistory();
    if (any(historyUV < 0.0) || any(historyUV > 1.0))
        return result;

    float2 historyTexel = historyUV * viewData.resolution.xy - 0.5;
    float2 historyOrigin = floor(historyTexel);
    float2 bilinearWeights = historyTexel - historyOrigin;
    float2 gatherUV = (historyOrigin + 1.0) * viewData.resolution.zw;

    float4 bilinearWeight4 = float4(
        (1.0 - bilinearWeights.x) * (1.0 - bilinearWeights.y),
        bilinearWeights.x * (1.0 - bilinearWeights.y),
        (1.0 - bilinearWeights.x) * bilinearWeights.y,
        bilinearWeights.x * bilinearWeights.y);

    float4 tapValid = float4(1.0, 1.0, 1.0, 1.0);
    if (useDepthTest)
    {
        float4 historyDeviceZ4 = SceneDepthHistory.GatherRed(PointClampSampler, gatherUV).wzxy;
        float4 historyLinearDepth4 = float4(
            reflectionLinearDepth(historyDeviceZ4.x, uViewData),
            reflectionLinearDepth(historyDeviceZ4.y, uViewData),
            reflectionLinearDepth(historyDeviceZ4.z, uViewData),
            reflectionLinearDepth(historyDeviceZ4.w, uViewData));
        tapValid = select(abs(historyLinearDepth4 - reprojectedLinearDepth) < depthThreshold, 1.0, 0.0);
    }

    float4 specularR4 = SpecularAndSecondMomentHistory.GatherRed(PointClampSampler, gatherUV).wzxy;
    float4 specularG4 = SpecularAndSecondMomentHistory.GatherGreen(PointClampSampler, gatherUV).wzxy;
    float4 specularB4 = SpecularAndSecondMomentHistory.GatherBlue(PointClampSampler, gatherUV).wzxy;
    float4 momentA4 = SpecularAndSecondMomentHistory.GatherAlpha(PointClampSampler, gatherUV).wzxy;

    float3 tap0 = float3(specularR4.x, specularG4.x, specularB4.x);
    float3 tap1 = float3(specularR4.y, specularG4.y, specularB4.y);
    float3 tap2 = float3(specularR4.z, specularG4.z, specularB4.z);
    float3 tap3 = float3(specularR4.w, specularG4.w, specularB4.w);

    float4 tapLightingValid = float4(
        isReflectionLightingValid(tap0) ? 1.0 : 0.0,
        isReflectionLightingValid(tap1) ? 1.0 : 0.0,
        isReflectionLightingValid(tap2) ? 1.0 : 0.0,
        isReflectionLightingValid(tap3) ? 1.0 : 0.0);

    float4 finalWeights = bilinearWeight4 * tapValid * tapLightingValid;
    float totalWeight = dot(finalWeights, float4(1.0, 1.0, 1.0, 1.0));
    if (totalWeight <= 0.0001)
        return result;

    tap0 = rgbToDenoiserSpace(tap0);
    tap1 = rgbToDenoiserSpace(tap1);
    tap2 = rgbToDenoiserSpace(tap2);
    tap3 = rgbToDenoiserSpace(tap3);

    result.valid = true;
    result.specular = (tap0 * finalWeights.x + tap1 * finalWeights.y + tap2 * finalWeights.z + tap3 * finalWeights.w) / totalWeight;
    result.secondMoment = dot(momentA4, finalWeights) / totalWeight;

    float4 frames4 = NumFramesAccumulatedHistory.GatherRed(PointClampSampler, gatherUV).wzxy;
    result.numFramesAccumulated = unpackNumFramesAccumulatedUnorm(dot(frames4, finalWeights) / totalWeight);
    return result;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 localCoord = dispatchThreadID.xy;
    if (any(localCoord >= PC.viewExtent))
        return;

    uint2 pixel = localCoord + PC.viewRectMin;
    float3 resolvedSpecular = ResolvedSpecularIndirect[pixel].rgb;

    if (!isReflectionLightingValid(resolvedSpecular))
    {
        NewSpecularAndSecondMoment[pixel] = float4(kReflectionInvalidLighting, 0.0);
        NewNumFramesAccumulated[pixel] = 0.0;
        return;
    }

    float3 newSpecular = rgbToDenoiserSpace(resolvedSpecular);
    float newSecondMoment = pow2(luminance(newSpecular));

    if (PC.historyValid == 0)
    {
        NewSpecularAndSecondMoment[pixel] = float4(resolvedSpecular, newSecondMoment);
        NewNumFramesAccumulated[pixel] = packNumFramesAccumulatedUnorm(1.0);
        return;
    }

    float rayDepth = RayDepth[pixel];
    float minRayHitT = ResolvedSpecularIndirectDepth[pixel];

    uint2 gbufferA = GbufferAImage[pixel];
    float4 gbufferB = GbufferBImage[pixel];
    float4 gbufferC = GbufferCImage[pixel];
    float4 gbufferD = GbufferDImage[pixel];

    float2 screenUV = (float2(pixel) + 0.5) * uViewData.resolution.zw;
    float3 currentViewPosition = reflectionViewPositionAtDepth(screenUV, rayDepth, uViewData);
    float3 viewDirWS = mul(uViewData.worldToViewRot, normalize(-currentViewPosition));

    ShadingParams sp = decodeShadingParams(gbufferA, gbufferB, gbufferC, gbufferD, viewDirWS);
    bool isClearCoat = sp.shadingModel == ShadingModel::Clear_Coat;
    float topLayerRoughness = isClearCoat ? sp.clearCoatRoughness : sp.roughness;
    float3 topLayerNormalWS = isClearCoat ? sp.clearCoatNormalWS : sp.normalWS;

    float preExposureCorrection = getPreExposure(uViewData) / max(uViewData.prevPreExposure, 0.0001);
    float noise = interleavedGradientNoise(float2(pixel) + 0.5, float(uFrameData.frameIndex % 8u));

    // 表面重投影——跟 ScreenProbeTemporalFilter.slang 同一套 NDC 速度重投影, 每个引擎的
    // Velocity 纹理已经把相机+物体运动一起算好, 不需要 UE 那套"静止像素退回纯相机矩阵"分支
    // (已核对, 见计划文档 §3.4)
    float4 encodedVelocity = Velocity[pixel];
    float2 velocityNDC = decodeVelocity(encodedVelocity).xy;
    float2 surfaceHistoryUV = screenUV - velocityNDC * float2(0.5, -0.5);

    float deviceZ = SceneDepthImage[pixel].r;
    float reprojectedDeviceZ = deviceZ - decodeVelocity(encodedVelocity).z;
    float reprojectedLinearDepth = reflectionLinearDepth(reprojectedDeviceZ, uViewData);
    float3 normalVS = mul(topLayerNormalWS, uViewData.worldToViewRot);
    float NoV = saturate(dot(normalVS, normalize(-currentViewPosition)));
    float surfaceDepthThreshold = reprojectedLinearDepth * kHistoryDistanceThreshold * lerp(0.5, 1.5, noise) / clamp(NoV, 0.1, 1.0);

    LightingHistory surfaceHistory = sampleHistory(surfaceHistoryUV, true, reprojectedLinearDepth, surfaceDepthThreshold, uViewData);
    if (surfaceHistory.valid)
    {
        surfaceHistory.specular *= preExposureCorrection;
        surfaceHistory.secondMoment *= pow2(preExposureCorrection);
    }

    // 命中点重投影——只在瓣形足够窄(反射方向比表面运动更重要)时取, UE SLC.ush:222-226
    LightingHistory hitHistory = initInvalidHistory();
    float specularDominantS = saturate(1.0 - topLayerRoughness);
    float specularDominantDirFactor = specularDominantS * (sqrt(specularDominantS) + topLayerRoughness);
    if (specularDominantDirFactor > 0.5 && minRayHitT > 0.0)
    {
        float virtualLinearDepth = rayDepth + minRayHitT;
        float3 virtualViewPosition = reflectionViewPositionAtDepth(screenUV, virtualLinearDepth, uViewData);

        float4 currentClip = mul(float4(currentViewPosition, 1.0), uViewData.viewToClipUnjittered);
        float3 measuredPrevNDC = currentClip.xyz / currentClip.w - decodeVelocity(encodedVelocity);

        float4 cameraOnlyPrevClipReal = mul(float4(reprojectCurrentViewPositionToPrev(currentViewPosition, uViewData), 1.0), uViewData.prevViewToClipUnjittered);
        float3 cameraOnlyPrevNDCReal = cameraOnlyPrevClipReal.xyz / cameraOnlyPrevClipReal.w;
        float3 objectMotionNDC = measuredPrevNDC - cameraOnlyPrevNDCReal;

        float4 cameraOnlyPrevClipVirtual = mul(float4(reprojectCurrentViewPositionToPrev(virtualViewPosition, uViewData), 1.0), uViewData.prevViewToClipUnjittered);
        float3 cameraOnlyPrevNDCVirtual = cameraOnlyPrevClipVirtual.xyz / cameraOnlyPrevClipVirtual.w;
        float3 hitPrevNDC = cameraOnlyPrevNDCVirtual + objectMotionNDC;

        float2 hitHistoryUV = float2(hitPrevNDC.x * 0.5 + 0.5, -hitPrevNDC.y * 0.5 + 0.5);
        hitHistory = sampleHistory(hitHistoryUV, false, 0.0, 0.0, uViewData);
        if (hitHistory.valid)
        {
            hitHistory.specular *= preExposureCorrection;
            hitHistory.secondMoment *= pow2(preExposureCorrection);
        }
    }

    // 邻域统计(5x5 去 4 角, 21 taps)——只统计有效样本, 无效样本对和/权重都不贡献
    float3 currentYCoCg = reflectionRGBToYCoCg(newSpecular);
    float3 sampleSum = currentYCoCg;
    float3 sampleSqSum = currentYCoCg * currentYCoCg;
    float weightSum = 1.0;
    [unroll]
    for (int offsetY = -2; offsetY <= 2; ++offsetY)
    {
        [unroll]
        for (int offsetX = -2; offsetX <= 2; ++offsetX)
        {
            if ((offsetX == 0 && offsetY == 0) || (abs(offsetX) == 2 && abs(offsetY) == 2))
                continue;

            int2 neighborCoord = int2(pixel) + int2(offsetX, offsetY);
            if (any(neighborCoord < int2(PC.viewRectMin)) || any(neighborCoord >= int2(PC.viewRectMin + PC.viewExtent)))
                continue;

            float3 neighborSpecular = ResolvedSpecularIndirect[uint2(neighborCoord)].rgb;
            if (!isReflectionLightingValid(neighborSpecular))
                continue;

            float3 neighborYCoCg = reflectionRGBToYCoCg(rgbToDenoiserSpace(neighborSpecular));
            sampleSum += neighborYCoCg;
            sampleSqSum += neighborYCoCg * neighborYCoCg;
            weightSum += 1.0;
        }
    }
    float3 neighborhoodMean = sampleSum / weightSum;
    float3 neighborhoodVariance = max(sampleSqSum / weightSum - neighborhoodMean * neighborhoodMean, 0.0);
    float3 neighborhoodExtent = kTemporalNeighborhoodClampScale * sqrt(neighborhoodVariance);
    float3 clampMin = neighborhoodMean - neighborhoodExtent;
    float3 clampMax = neighborhoodMean + neighborhoodExtent;

    float3 hitYCoCgClamped = clamp(reflectionRGBToYCoCg(hitHistory.specular), clampMin, clampMax);
    float3 surfaceYCoCgClamped = clamp(reflectionRGBToYCoCg(surfaceHistory.specular), clampMin, clampMax);
    float3 hitYCoCgRaw = reflectionRGBToYCoCg(hitHistory.specular);
    float3 surfaceYCoCgRaw = reflectionRGBToYCoCg(surfaceHistory.specular);

    // 哪份历史跟邻域中心误差小用哪份——命中点历史误差比表面历史大 0.01 以上时更依赖表面
    // (但不完全放弃命中点历史, 软化过渡), 命中点历史无效时纯表面
    float surfaceHistoryAlpha = 0.0;
    float hitError = length(neighborhoodMean - hitYCoCgRaw);
    float surfaceError = length(neighborhoodMean - surfaceYCoCgRaw);
    if (hitError > surfaceError + 0.01)
        surfaceHistoryAlpha = 0.8;
    if (!hitHistory.valid)
        surfaceHistoryAlpha = 1.0;

    float3 clampedYCoCg = lerp(hitYCoCgClamped, surfaceYCoCgClamped, surfaceHistoryAlpha);
    float3 rawYCoCg = lerp(hitYCoCgRaw, surfaceYCoCgRaw, surfaceHistoryAlpha);
    float clampedSecondMoment = lerp(hitHistory.secondMoment, surfaceHistory.secondMoment, surfaceHistoryAlpha);

    // 每个 YCoCg 分量各自按自己的 extent 归一化(不是除以三分量里最大的那个), 跟 UE
    // NormalizedDistanceToNeighborhood 的写法一致
    float3 normalizedDistance = abs(clampedYCoCg - rawYCoCg) / max(neighborhoodExtent, 0.1);
    float confidence = saturate(1.0 - length(normalizedDistance));
    confidence = 0.75 * confidence + 0.25;

    bool useSurfaceFrames = surfaceHistoryAlpha > 0.5;
    float historyFrames = useSurfaceFrames ? surfaceHistory.numFramesAccumulated : hitHistory.numFramesAccumulated;
    if (!surfaceHistory.valid && !hitHistory.valid)
        historyFrames = 0.0;

    float maxFramesForRoughness = getMaxFramesAccumulated(kMaxFramesAccumulated, topLayerRoughness);
    float numFramesAccumulated = min(historyFrames * confidence + 1.0, maxFramesForRoughness);
    float alpha = 1.0 / numFramesAccumulated;

    // 两份历史都无效时 historyFrames=0, numFramesAccumulated 因此钳到 1、alpha=1, lerp 自然
    // 退化成纯当前样本, 不需要额外分支特判
    float3 clampedSpecular = reflectionYCoCgToRGB(clampedYCoCg);
    float3 finalSpecular = max(makeFinite(lerp(clampedSpecular, newSpecular, alpha)), 0.0);
    float finalSecondMoment = max(lerp(clampedSecondMoment, newSecondMoment, alpha), 0.0);

    NewSpecularAndSecondMoment[pixel] = float4(denoiserSpaceToRGB(finalSpecular), finalSecondMoment);
    NewNumFramesAccumulated[pixel] = packNumFramesAccumulatedUnorm(numFramesAccumulated);
}
