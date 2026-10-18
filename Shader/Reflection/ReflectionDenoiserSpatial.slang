// 对应 UE LumenReflectionDenoiserSpatialCS(LRDS.usf), 见 Document/Lumen_Reflections_Plan.md §3.5
#include "../Common/Binding.slang"
#include "../Common/GBufferEncoding.slang"
#include "../Common/MaterialIO.slang"
#include "../Common/Math.slang"
#include "../Common/MonteCarlo.slang"
#include "../Material/ShadingParams.slang"
#include "ReflectionDenoiserCommon.slang"
#include "ReflectionCommon.slang"

[[vk::binding(0, 2)]]
Texture2D<float4> SpecularAndSecondMoment;

[[vk::binding(1, 2)]]
Texture2D<float> NumFramesAccumulated;

[[vk::binding(2, 2)]]
Texture2D<uint2> GbufferAImage;

[[vk::binding(3, 2)]]
Texture2D<float4> GbufferBImage;

[[vk::binding(4, 2)]]
Texture2D<float4> GbufferCImage;

[[vk::binding(5, 2)]]
Texture2D<float4> GbufferDImage;

[[vk::binding(6, 2)]]
DepthTexture2D SceneDepthImage;

[[vk::binding(7, 2)]]
[format("r11f_g11f_b10f")]
RWTexture2D<float4> SpecularIndirect;

struct ReflectionDenoiserSpatialPushConstants
{
    uint2 viewRectMin;
    uint2 viewExtent;
};
[[vk::push_constant]]
ReflectionDenoiserSpatialPushConstants PC;

// UE r.Lumen.Reflections.BilateralFilter.KernelRadius / NumSamples / DepthWeightScale / MaxDisocclusionFrames 默认值
static const float kSpatialFilterKernelRadius = 8.0;
static const float kSpatialFilterNumSamples = 4.0;
static const float kSpatialFilterDepthWeightScale = 10000.0;
static const float kSpatialFilterMaxDisocclusionFrames = 2.0;

// UE r.Lumen.Reflections.Temporal.MaxFramesAccumulated 默认值
static const float kTemporalMaxFramesAccumulated = 12.0;

struct SpatialMaterial
{
    float linearDepth;
    float3 viewPosition;
    float3 normalVS;
    float topLayerRoughness;
};

SpatialMaterial readSpatialMaterial(uint2 pixel)
{
    float2 screenUV = (float2(pixel) + 0.5) * uViewData.resolution.zw;

    SpatialMaterial material;
    material.linearDepth = reflectionLinearDepth(SceneDepthImage[pixel].r, uViewData);
    material.viewPosition = reflectionViewPositionAtDepth(screenUV, material.linearDepth, uViewData);
    float3 viewDirWS = mul(uViewData.worldToViewRot, normalize(-material.viewPosition));

    ShadingParams sp = decodeShadingParams(GbufferAImage[pixel], GbufferBImage[pixel], GbufferCImage[pixel], GbufferDImage[pixel], viewDirWS);
    bool isClearCoat = sp.shadingModel == ShadingModel::Clear_Coat;
    material.topLayerRoughness = isClearCoat ? sp.clearCoatRoughness : sp.roughness;
    material.normalVS = mul(isClearCoat ? sp.clearCoatNormalWS : sp.normalWS, uViewData.worldToViewRot);
    return material;
}

float3 tonemapLighting(float3 lighting, float disocclusionFactor)
{
    return lighting / (1.0 + disocclusionFactor * luminance(lighting));
}

float3 inverseTonemapLighting(float3 tonemappedLighting, float disocclusionFactor)
{
    return tonemappedLighting / (1.0 - disocclusionFactor * luminance(tonemappedLighting));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 localCoord = dispatchThreadID.xy;
    if (any(localCoord >= PC.viewExtent))
        return;

    uint2 pixel = localCoord + PC.viewRectMin;

    float4 centerSpecularAndSecondMoment = SpecularAndSecondMoment[pixel];
    if (!isReflectionLightingValid(centerSpecularAndSecondMoment.xyz))
    {
        SpecularIndirect[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float3 centerSpecular = rgbToDenoiserSpace(centerSpecularAndSecondMoment.xyz);
    float centerLuminance = luminance(centerSpecular);
    float centerStdDev = sqrt(max(centerSpecularAndSecondMoment.w - pow2(centerLuminance), 0.0));

    SpatialMaterial center = readSpatialMaterial(pixel);

    float numFramesAccumulated = NumFramesAccumulated[pixel] * kTemporalMaxFramesAccumulated;
    float disocclusionFactor = 1.0 - saturate(numFramesAccumulated / kSpatialFilterMaxDisocclusionFrames);

    float3 specularSum = tonemapLighting(centerSpecular, disocclusionFactor);
    float weightSum = 1.0;

    float kernelRadiusInPixels = kSpatialFilterKernelRadius * saturate(center.topLayerRoughness * 8.0);
    uint numSamples = uint(lerp(kSpatialFilterNumSamples, 2.0 * kSpatialFilterNumSamples, disocclusionFactor) + 0.5);

    // 2x2 共享随机种子, 提高相邻像素的一致性
    uint2 randomSeed = rand3DPCG16(int3(int2(pixel % 2u), int(uFrameData.frameIndex % 8u))).xy;

    bool filterSpecular = centerStdDev / max(centerLuminance, 0.1) > 0.5 || disocclusionFactor > 0.01;

    if (kernelRadiusInPixels > 1.0 && filterSpecular)
    {
        float lobeHalfAngle = getSpecularLobeHalfAngle(center.topLayerRoughness) + 0.01;
        lobeHalfAngle *= lerp(1.0, 4.0, disocclusionFactor);
        lobeHalfAngle = clamp(lobeHalfAngle, 0.01, 0.5 * PI);

        for (uint neighborIndex = 0; neighborIndex < numSamples; ++neighborIndex)
        {
            float2 offsetInRect = hammersley16(neighborIndex, numSamples, randomSeed);
            float2 offset = uniformSampleDiskConcentric(offsetInRect) * kernelRadiusInPixels;
            int2 neighborCoord = int2(float2(pixel) + offset + 0.5);

            if (any(neighborCoord < int2(PC.viewRectMin)) || any(neighborCoord >= int2(PC.viewRectMin + PC.viewExtent)))
                continue;

            uint2 neighborPixel = uint2(neighborCoord);
            float3 neighborRaw = SpecularAndSecondMoment[neighborPixel].xyz;
            if (!isReflectionLightingValid(neighborRaw))
                continue;

            SpatialMaterial neighbor = readSpatialMaterial(neighborPixel);

            float planeDistance = abs(dot(neighbor.viewPosition - center.viewPosition, center.normalVS));
            float relativeDepthDifference = planeDistance / center.linearDepth;
            float depthWeight = exp2(-kSpatialFilterDepthWeightScale * pow2(relativeDepthDifference));

            float angleBetweenNormals = acosFast(saturate(dot(center.normalVS, neighbor.normalVS)));
            float normalWeight = 1.0 - saturate(angleBetweenNormals / lobeHalfAngle);

            float3 neighborSpecular = rgbToDenoiserSpace(neighborRaw);
            float luminanceDelta = abs(centerLuminance - luminance(neighborSpecular));
            float luminanceWeight = exp2(-luminanceDelta / max(centerStdDev, 0.001));
            luminanceWeight = lerp(luminanceWeight, 1.0, disocclusionFactor);

            float neighborWeight = depthWeight * normalWeight * luminanceWeight;
            specularSum += tonemapLighting(neighborSpecular, disocclusionFactor) * neighborWeight;
            weightSum += neighborWeight;
        }
    }

    float3 specular = inverseTonemapLighting(specularSum / weightSum, disocclusionFactor);
    SpecularIndirect[pixel] = float4(denoiserSpaceToRGB(specular), 1.0);
}
