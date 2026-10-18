// 对应 UE LumenReflectionResolveCS(LRR.usf),降采样系数固定 1,中心样本直接取 R2 的 TraceRadiance;
// 邻域重建(默认开)按 BRDF/PDF 重新加权 5 个邻域样本,粗糙度 <= 0.05 或重建核半径 <= 1 像素时
// 直接透传中心样本,不做任何重建。见 Document/Lumen_Reflections_Plan.md §3.3
#include "../Common/Binding.slang"
#include "../Common/GBufferEncoding.slang"
#include "../Common/MaterialIO.slang"
#include "../Common/MonteCarlo.slang"
#include "../Material/ShadingParams.slang"
#include "../Material/BxDF.slang"
#include "ReflectionDenoiserCommon.slang"
#include "ReflectionCommon.slang"

[[vk::binding(0, 2)]]
Texture2D<float4> RayBuffer;

[[vk::binding(1, 2)]]
Texture2D<float> RayDepth;

[[vk::binding(2, 2)]]
Texture2D<float4> TraceRadiance;

[[vk::binding(3, 2)]]
Texture2D<float> TraceHit;

[[vk::binding(4, 2)]]
Texture2D<uint2> GbufferAImage;

[[vk::binding(5, 2)]]
Texture2D<float4> GbufferBImage;

[[vk::binding(6, 2)]]
Texture2D<float4> GbufferCImage;

[[vk::binding(7, 2)]]
Texture2D<float4> GbufferDImage;

[[vk::binding(8, 2)]]
[format("r11f_g11f_b10f")]
RWTexture2D<float4> ResolvedSpecularIndirect;

[[vk::binding(9, 2)]]
[format("r16f")]
RWTexture2D<float> ResolvedSpecularIndirectDepth;

struct ReflectionResolvePushConstants
{
    uint2 viewRectMin;
    uint2 viewExtent;
};
[[vk::push_constant]]
ReflectionResolvePushConstants PC;

// UE r.Lumen.Reflections.ScreenSpaceReconstruction.KernelRadius 默认值
static const float kSpatialReconstructionKernelRadius = 8.0;

// UE r.Lumen.Reflections.ScreenSpaceReconstruction.NumSamples 默认值(Quality=1 时)
static const uint kNumSpatialReconstructionSamples = 5u;

// UE GetSampleWeight()(LRR.usf:58-73): BRDF(采样方向)/该方向的采样 PDF
float tangentSampleWeight(float3 rayDirVS, float pdf, in float3x3 tangentBasisVS, float3 viewDirVS, bool useAnisotropy, float a2, float2 axAy)
{
    float3 h = mul(tangentBasisVS, normalize(viewDirVS + rayDirVS));
    float ndf = useAnisotropy ? D_GGXaniso(axAy.x, axAy.y, h.z, h.x, h.y) : D_GGX(h.z, a2);
    return ndf / max(pdf, 1e-6);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 localCoord = dispatchThreadID.xy;
    if (any(localCoord >= PC.viewExtent))
        return;

    uint2 pixel = localCoord + PC.viewRectMin;
    float centerRayDepth = RayDepth[pixel];
    if (!(centerRayDepth > 0.0))
    {
        ResolvedSpecularIndirect[pixel] = float4(kReflectionInvalidLighting, 1.0);
        ResolvedSpecularIndirectDepth[pixel] = -1.0;
        return;
    }

    float3 centerLighting = rgbToDenoiserSpace(TraceRadiance[pixel].rgb);
    float centerWeightSum = 1.0;
    bool centerHitFlag;
    float minRayHitT = decodeRayDistance(TraceHit[pixel], centerHitFlag);
    // 邻域命中距离的钳位基准, 整个邻域循环期间固定不变——不能用下面逐步更新的 minRayHitT,
    // 否则越往后处理的邻域被钳得越死, 跟 UE CenterRayHitT 的固定语义不一致
    const float centerRayHitT = minRayHitT;

    uint2 gbufferA = GbufferAImage[pixel];
    float4 gbufferB = GbufferBImage[pixel];
    float4 gbufferC = GbufferCImage[pixel];
    float4 gbufferD = GbufferDImage[pixel];

    float2 screenUV = (float2(pixel) + 0.5) * uViewData.resolution.zw;
    float3 centerViewPosition = reflectionViewPositionAtDepth(screenUV, centerRayDepth, uViewData);
    float3 viewDirVS = normalize(-centerViewPosition);
    float3 viewDirWS = mul(uViewData.worldToViewRot, viewDirVS);

    ShadingParams sp = decodeShadingParams(gbufferA, gbufferB, gbufferC, gbufferD, viewDirWS);
    bool isClearCoat = sp.shadingModel == ShadingModel::Clear_Coat;
    float topLayerRoughness = isClearCoat ? sp.clearCoatRoughness : sp.roughness;
    float3 topLayerNormalWS = isClearCoat ? sp.clearCoatNormalWS : sp.normalWS;
    bool useAnisotropy = sp.hasAnisotropy && !isClearCoat;

    float kernelRadiusInPixels = 0.0;
    if (topLayerRoughness > 0.05)
    {
        kernelRadiusInPixels = kSpatialReconstructionKernelRadius * saturate(topLayerRoughness * 8.0);
    }

    // 降采样系数固定 1, 核半径不超过 1 像素时重建没有意义(镜面反射也会落在这个分支, 直接透传)
    if (kernelRadiusInPixels > 1.0)
    {
        float3 topLayerNormalVS = mul(topLayerNormalWS, uViewData.worldToViewRot);
        float3x3 tangentBasisVS;
        if (useAnisotropy)
        {
            float3 anisotropyTangentVS = mul(sp.anisotropyTangentWS, uViewData.worldToViewRot);
            tangentBasisVS = float3x3(anisotropyTangentVS, cross(topLayerNormalVS, anisotropyTangentVS), topLayerNormalVS);
        }
        else
        {
            tangentBasisVS = getTangentBasis(topLayerNormalVS);
        }

        float alpha = pow2(topLayerRoughness);
        float2 axAy = float2(alpha, alpha);
        if (useAnisotropy)
        {
            getAnisotropicRoughness(alpha, sp.anisotropyStrength, axAy.x, axAy.y);
        }
        float a2 = axAy.x * axAy.y;

        float2 kernelDiameter = float2(kernelRadiusInPixels, kernelRadiusInPixels);
        float2 kernelMajorAxis = float2(kernelDiameter.x, 0.0);
        float2 kernelMinorAxis = float2(0.0, kernelDiameter.y);
        if (useAnisotropy)
        {
            // 各向异性强度是无符号量(0..1), 符号已经折进 tangentBasisVS[0] 的方向里——所以核
            // x 方向(切线方向)恒定是拉长的那一边, 跟 getAnisotropicRoughness() 里 ax>=ay 的
            // 约定一致, 不需要区分正负各向异性
            kernelDiameter.x = max(kernelDiameter.x * (1.0 + sp.anisotropyStrength), 1.0);
            kernelDiameter.y = max(kernelDiameter.y * (1.0 - sp.anisotropyStrength), 1.0);

            float4 projX = mul(float4(centerViewPosition + tangentBasisVS[0], 1.0), uViewData.viewToClip);
            float4 projY = mul(float4(centerViewPosition + tangentBasisVS[1], 1.0), uViewData.viewToClip);
            projX /= projX.w;
            projY /= projY.w;
            float2 projXPixel = (float2(projX.x, -projX.y) * 0.5 + 0.5) * uViewData.resolution.xy;
            float2 projYPixel = (float2(projY.x, -projY.y) * 0.5 + 0.5) * uViewData.resolution.xy;

            kernelMajorAxis = kernelDiameter.x * normalize(projXPixel - (float2(pixel) + 0.5));
            kernelMinorAxis = kernelDiameter.y * normalize(projYPixel - (float2(pixel) + 0.5));
        }

        float3 centerRayDirVS = mul(RayBuffer[pixel].xyz, uViewData.worldToViewRot);
        float centerPdf = 1.0 / max(RayBuffer[pixel].w, 0.0001);
        float centerSampleWeight = max(
            tangentSampleWeight(centerRayDirVS, centerPdf, tangentBasisVS, viewDirVS, useAnisotropy, a2, axAy), 0.001);
        centerLighting *= centerSampleWeight;
        centerWeightSum = centerSampleWeight;

        uint2 randomSeed = rand3DPCG16(int3(int2(pixel), int(uFrameData.frameIndex % 8u))).xy;

        [unroll]
        for (uint sampleIndex = 0; sampleIndex < kNumSpatialReconstructionSamples; ++sampleIndex)
        {
            float2 offsetInRect = hammersley16(sampleIndex, kNumSpatialReconstructionSamples, randomSeed);
            float2 offset = uniformSampleDiskConcentric(offsetInRect);
            offset = offset.x * kernelMajorAxis + offset.y * kernelMinorAxis;
            int2 neighborCoord = int2(float2(pixel) + offset + 0.5);

            if (any(neighborCoord < int2(PC.viewRectMin)) || any(neighborCoord >= int2(PC.viewRectMin + PC.viewExtent)))
                continue;

            uint2 neighborPixel = uint2(neighborCoord);
            float neighborRayDepth = RayDepth[neighborPixel];
            if (!(neighborRayDepth > 0.0))
                continue;

            float2 neighborScreenUV = (float2(neighborPixel) + 0.5) * uViewData.resolution.zw;
            float3 neighborViewPosition = reflectionViewPositionAtDepth(neighborScreenUV, neighborRayDepth, uViewData);
            float4 neighborRay = RayBuffer[neighborPixel];
            float3 neighborRayDirVS = mul(neighborRay.xyz, uViewData.worldToViewRot);
            float neighborPdf = 1.0 / max(neighborRay.w, 0.0001);

            bool neighborHitFlag;
            float neighborHitDistance = decodeRayDistance(TraceHit[neighborPixel], neighborHitFlag);
            // 保留接触, 避免偏向打到背景的样本
            neighborHitDistance = min(neighborHitDistance, centerRayHitT);

            float3 neighborHitPositionVS = neighborViewPosition + neighborRayDirVS * neighborHitDistance;
            float3 offsetToHit = neighborHitPositionVS - centerViewPosition;
            float distanceToHit = length(offsetToHit);
            float3 directionToHitVS = distanceToHit > 0.0 ? offsetToHit / distanceToHit : neighborRayDirVS;

            float sampleWeight = tangentSampleWeight(directionToHitVS, neighborPdf, tangentBasisVS, viewDirVS, useAnisotropy, a2, axAy);
            if (sampleWeight <= 1e-6)
                continue;

            float3 neighborLighting = rgbToDenoiserSpace(TraceRadiance[neighborPixel].rgb);
            centerLighting += neighborLighting * sampleWeight;
            centerWeightSum += sampleWeight;
            minRayHitT = min(minRayHitT, neighborHitDistance);
        }
    }

    if (centerWeightSum > 1e-6)
    {
        centerLighting /= centerWeightSum;
    }
    centerLighting = denoiserSpaceToRGB(centerLighting);

    ResolvedSpecularIndirect[pixel] = float4(centerLighting, 1.0);
    ResolvedSpecularIndirectDepth[pixel] = minRayHitT;
}
