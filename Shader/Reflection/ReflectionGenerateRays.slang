// 对应 UE ReflectionGenerateRaysCS(LR.usf:301-436),降采样系数固定 1;不含 Radiance Cache
// (未实现)、Substrate 重要性采样(不适用)分支。见 Document/Lumen_Reflections_Plan.md §3.1
#include "../Common/Binding.slang"
#include "../Common/GBufferEncoding.slang"
#include "../Common/MaterialIO.slang"
#include "../Common/MonteCarlo.slang"
#include "../Common/RadiosityProbeSampling.slang"
#include "../Material/ShadingParams.slang"
#include "../Material/BxDF.slang"
#include "ReflectionCommon.slang"

[[vk::binding(0, 2)]]
DepthTexture2D SceneDepthImage;

[[vk::binding(1, 2)]]
Texture2D<uint2> GbufferAImage;

[[vk::binding(2, 2)]]
Texture2D<float4> GbufferBImage;

[[vk::binding(3, 2)]]
Texture2D<float4> GbufferCImage;

[[vk::binding(4, 2)]]
Texture2D<float4> GbufferDImage;

[[vk::binding(5, 2)]]
Texture2D<float4> BlueNoiseVec2Texture;

[[vk::binding(6, 2)]]
[format("rgba16f")]
RWTexture2D<float4> RayBuffer;

[[vk::binding(7, 2)]]
[format("r32f")]
RWTexture2D<float> RayDepth;

struct ReflectionGenerateRaysPushConstants
{
    uint2 viewRectMin;
    uint2 viewExtent;
};
[[vk::push_constant]]
ReflectionGenerateRaysPushConstants PC;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 localCoord = dispatchThreadID.xy;
    if (any(localCoord >= PC.viewExtent))
        return;

    uint2 pixel = localCoord + PC.viewRectMin;
    float deviceZ = SceneDepthImage[pixel].r;
    if (deviceZ == 0.0)
    {
        RayBuffer[pixel] = float4(0.0, 0.0, 0.0, 0.0);
        RayDepth[pixel] = -1.0;
        return;
    }

    float linearDepth = reflectionLinearDepth(deviceZ, uViewData);

    float2 screenUV = (float2(pixel) + 0.5) * uViewData.resolution.zw;
    float4 fragPosCS = float4(screenUV.x * 2.0 - 1.0, (1.0 - screenUV.y) * 2.0 - 1.0, deviceZ, 1.0);
    float4 fragPosVS = mul(fragPosCS, uViewData.clipToView);
    fragPosVS *= 1.0 / fragPosVS.w;
    float3 viewDirVS = normalize(-fragPosVS.xyz);
    float3 viewDirWS = mul(uViewData.worldToViewRot, viewDirVS);

    uint2 gbufferA = GbufferAImage[pixel];
    float4 gbufferB = GbufferBImage[pixel];
    float4 gbufferC = GbufferCImage[pixel];
    float4 gbufferD = GbufferDImage[pixel];
    ShadingParams sp = decodeShadingParams(gbufferA, gbufferB, gbufferC, gbufferD, viewDirWS);

    // 清漆像素追专属光线用的是顶层(清漆)法线/粗糙度, 不是底层——R8 已核对的角色映射见计划文档 §3.8
    bool isClearCoat = sp.shadingModel == ShadingModel::Clear_Coat;
    float topLayerRoughness = isClearCoat ? sp.clearCoatRoughness : sp.roughness;
    float3 topLayerNormalWS = isClearCoat ? sp.clearCoatNormalWS : sp.normalWS;

    if (sp.shadingModel == ShadingModel::Unlit || !needsReflectionRay(topLayerRoughness, isClearCoat))
    {
        RayBuffer[pixel] = float4(0.0, 0.0, 0.0, 0.0);
        RayDepth[pixel] = -linearDepth;
        return;
    }

    float3 cameraVector = -viewDirWS;
    float3 rayDirWS;
    float coneAngle = 0.0;

    if (topLayerRoughness < 0.001)
    {
        rayDirWS = reflect(cameraVector, topLayerNormalWS);
    }
    else
    {
        float2 e = getRadiosityProbeTexelCenter(BlueNoiseVec2Texture, uFrameData.frameIndex, pixel);
        e.y *= 1.0 - kReflectionGGXSamplingBias;

        // 清漆的顶层没有各向异性(UE 清漆和各向异性是互斥的两个 legacy 着色模型)
        bool useAnisotropy = sp.hasAnisotropy && !isClearCoat;
        float3x3 tangentBasis = useAnisotropy
            ? float3x3(sp.anisotropyTangentWS, cross(topLayerNormalWS, sp.anisotropyTangentWS), topLayerNormalWS)
            : getTangentBasis(topLayerNormalWS);
        float3 tangentV = mul(tangentBasis, viewDirWS);

        float alpha = pow2(topLayerRoughness);
        float2 anisoAlpha = float2(alpha, alpha);
        if (useAnisotropy)
        {
            getAnisotropicRoughness(alpha, sp.anisotropyStrength, anisoAlpha.x, anisoAlpha.y);
        }

        float4 ggxSample = importanceSampleVisibleGGX(e, anisoAlpha, tangentV);
        float3 worldH = mul(ggxSample.xyz, tangentBasis);
        rayDirWS = reflect(cameraVector, worldH);
        coneAngle = 1.0 / max(ggxSample.w, 0.0001);
    }

    coneAngle = max(coneAngle, kReflectionMinConeAngle);
    RayBuffer[pixel] = float4(rayDirWS, coneAngle);
    RayDepth[pixel] = linearDepth;
}
