// 对应 UE ScreenProbeIntegrateCS 的 SIMPLE_DIFFUSE 路径、两种辐照度格式(LumenScreenProbeGather.usf:1130-1495), 见 Document/Lumen_ScreenProbeGather_Plan.md §6.6。
#include "../Common/Binding.slang"
#include "../Common/GBufferEncoding.slang"
#include "../Common/MaterialIO.slang"
#include "../Material/ShadingParams.slang"
#include "../Material/BxDF.slang"
#include "../Reflection/ReflectionCommon.slang"
#include "ScreenProbeCommon.slang"
#include "ScreenProbeIrradianceFormat.slang"
#include "ScreenProbeIrradianceSH.slang"

[[vk::binding(0, 2)]]
DepthTexture2D SceneDepthImage;

[[vk::binding(1, 2)]]
Texture2D<uint2> GbufferAImage;

[[vk::binding(2, 2)]]
Texture2D<float4> GbufferBImage;

[[vk::binding(12, 2)]]
Texture2D<float4> GbufferCImage;

[[vk::binding(13, 2)]]
Texture2D<float4> GbufferDImage;

[[vk::binding(14, 2)]]
Texture2D<float4> ScreenProbeRadianceWithBorder;

[[vk::binding(7, 2)]]
[format("r11f_g11f_b10f")]
RWTexture2D<float4> RoughSpecularIndirect;

[[vk::binding(3, 2)]]
Texture2D<float> ScreenProbeSceneDepth;

#if SCREEN_PROBE_IRRADIANCE_FORMAT == SCREEN_PROBE_IRRADIANCE_FORMAT_SH3

[[vk::binding(4, 2)]]
Texture2D<float4> ScreenProbeRadianceSHAmbient;

[[vk::binding(15, 2)]]
Texture2D<float4> ScreenProbeRadianceSHDirectional;

#else

[[vk::binding(4, 2)]]
Texture2D<float4> ScreenProbeIrradianceWithBorder;

#endif

[[vk::binding(5, 2)]]
StructuredBuffer<uint> AdaptiveScreenProbePosAndTileIndices;

[[vk::binding(6, 2)]]
StructuredBuffer<uint> AdaptiveScreenProbeTotalAndTileCounts;

[[vk::binding(8, 2)]]
Texture2D<float4> BlueNoiseVec2Texture;

[[vk::binding(9, 2)]]
SamplerState LinearSampler;

[[vk::binding(10, 2)]]
[format("r11f_g11f_b10f")]
RWTexture2D<float4> DiffuseIndirect;

// 探针插值权重(主/备用)是否达到 kMinProbeInterpolationWeight, 对应 UE RWLightIsMoving 的有效性位
[[vk::binding(11, 2)]]
[format("r8")]
RWTexture2D<float> LightingValid;

struct ScreenProbeIntegratePushConstants
{
    uint2 viewRectMin;
    uint2 viewExtent;
    uint2 tileGridSize;
    uint2 atlasViewSize;
    uint maxNumAdaptiveProbes;
};
[[vk::push_constant]]
ScreenProbeIntegratePushConstants PC;

// UE r.Lumen.ScreenProbeGather.FullResolutionJitterWidth 默认值
static const float kFullResolutionJitterWidth = 1.0;

// UE 抖动宽度随深度衰减的区间 [500, 1000] cm(LumenScreenProbeGather.usf:1172), 换算成米
static const float kJitterWidthFadeStartDepth = 5.0;
static const float kJitterWidthFadeLength = 5.0;

// UE r.Lumen.ScreenProbeGather.MaxRoughnessToEvaluateRoughSpecular 默认值
static const float kMaxRoughnessToEvaluateRoughSpecular = 0.8;

// UE RoughReflectionsDiffuseLerp 里写死的淡出长度(LumenScreenProbeTileClassication.ush:36)
static const float kRoughSpecularFadeLength = 0.2;

static const uint kNumRoughSpecularSamples = 4;

// UE RoughReflectionsDiffuseLerp(LumenScreenProbeTileClassication.ush:25-37), 1 表示整段跳过粗糙镜面、直接复用漫反射
float roughReflectionsDiffuseLerp(float roughness, bool isClearCoat)
{
    if (reflectionCombineAlpha(roughness) >= 1.0 && !isClearCoat)
        return 1.0;

    return saturate((roughness - kMaxRoughnessToEvaluateRoughSpecular + kRoughSpecularFadeLength) / kRoughSpecularFadeLength);
}

float3 tonemapLighting(float3 lighting)
{
    return lighting / (1.0 + dot(lighting, float3(0.2126, 0.7152, 0.0722)));
}

float3 inverseTonemapLighting(float3 tonemappedLighting)
{
    return tonemappedLighting / (1.0 - dot(tonemappedLighting, float3(0.2126, 0.7152, 0.0722)));
}

float3 sampleProbeRadiance(float2 probeUV, uint2 atlasCoord)
{
    float2 probeTexel = probeUV * float(kScreenProbeGatherOctahedronResolution) + float(kScreenProbeRadianceBorderSize);
    float2 atlasUV = (float2(atlasCoord * kScreenProbeRadianceWithBorderResolution) + probeTexel)
        / float2(PC.atlasViewSize * kScreenProbeRadianceWithBorderResolution);
    return ScreenProbeRadianceWithBorder.SampleLevel(LinearSampler, atlasUV, 0.0).rgb;
}

// UE InterpolateFromScreenProbes 的 !STOCHASTIC_PROBE_INTERPOLATION 分支(LumenScreenProbeGather.usf:875-906)
float3 interpolateRadianceFromProbes(float3 direction, ScreenProbeUpsampleWeights probeSample, float4 weights)
{
    float2 probeUV = inverseEquiAreaSphericalMapping(direction);
    float3 radiance = float3(0.0);
    for (uint cornerIndex = 0; cornerIndex < 4; ++cornerIndex)
    {
        if (weights[cornerIndex] > 0.0)
            radiance += sampleProbeRadiance(probeUV, probeSample.atlasCoord[cornerIndex]) * weights[cornerIndex];
    }
    return radiance;
}

ThreeBandSHRGB scaleSH3(ThreeBandSHRGB sh, float scale)
{
    ThreeBandSHRGB result;
    result.r.v0 = sh.r.v0 * scale;
    result.r.v1 = sh.r.v1 * scale;
    result.r.v2 = sh.r.v2 * scale;
    result.g.v0 = sh.g.v0 * scale;
    result.g.v1 = sh.g.v1 * scale;
    result.g.v2 = sh.g.v2 * scale;
    result.b.v0 = sh.b.v0 * scale;
    result.b.v1 = sh.b.v1 * scale;
    result.b.v2 = sh.b.v2 * scale;
    return result;
}

// UE LumenScreenProbeGather.usf:1502-1562(ROUGH_SPECULAR_ALONG_REFLECTION_DIRECTION = 0 分支)
float3 integrateRoughSpecular(in ShadingParams sp, float3 diffuseIndirect, ScreenProbeUpsampleWeights probeSample, float4 weights, bool lightingIsValid, uint2 screenCoord)
{
    bool isClearCoat = sp.shadingModel == ShadingModel::Clear_Coat;
    float diffuseLerp = roughReflectionsDiffuseLerp(sp.roughness, isClearCoat);
    if (diffuseLerp >= 1.0)
        return diffuseIndirect;

    // 探针只有 8x8 分辨率, 粗糙度太低的表面(比如清漆底层)支撑不了, 钳到约 2x2 texel 的覆盖范围
    float roughness = max(sp.roughness, 0.2);
    bool useAnisotropy = sp.hasAnisotropy && !isClearCoat;

    float2 alpha = float2(pow2(roughness));
    float3x3 tangentBasis;
    if (useAnisotropy)
    {
        getAnisotropicRoughness(alpha.x, sp.anisotropyStrength, alpha.x, alpha.y);
        tangentBasis = float3x3(sp.anisotropyTangentWS, cross(sp.normalWS, sp.anisotropyTangentWS), sp.normalWS);
    }
    else
    {
        tangentBasis = getTangentBasis(sp.normalWS);
    }

    float3 tangentV = mul(tangentBasis, sp.viewDirWS);
    uint2 randomSeed = rand3DPCG16(int3(int2(screenCoord), int(screenProbeFrameIndex() % 8u))).xy;

    float3 roughSpecular = float3(0.0);
    for (uint sampleIndex = 0; sampleIndex < kNumRoughSpecularSamples; ++sampleIndex)
    {
        float2 e = hammersley16(sampleIndex, kNumRoughSpecularSamples, randomSeed);
        // UE BiasBSDFImportantSample: 收窄 E.y 的取值范围, 避开 VNDF 采样的极端方向
        e.y = (e.y - 0.5) * 0.9 + 0.5;

        float3 h = mul(importanceSampleVisibleGGX(e, alpha, tangentV).xyz, tangentBasis);
        float3 sampleDirection = 2.0 * dot(sp.viewDirWS, h) * h - sp.viewDirWS;

        float3 radiance = lightingIsValid ? interpolateRadianceFromProbes(sampleDirection, probeSample, weights) : float3(0.0);
        roughSpecular += tonemapLighting(radiance);
    }
    roughSpecular = inverseTonemapLighting(roughSpecular / float(kNumRoughSpecularSamples));

    return lerp(roughSpecular, diffuseIndirect, diffuseLerp);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= PC.viewExtent))
        return;

    uint2 screenCoord = pixel + PC.viewRectMin;

    float deviceZ = SceneDepthImage[screenCoord].r;
    ShadingModel shadingModel;
    bool hasAnisotropy;
    unpackShadingModelByte(GbufferBImage[screenCoord].w, shadingModel, hasAnisotropy);
    if (deviceZ == 0.0 || shadingModel == ShadingModel::Unlit)
    {
        DiffuseIndirect[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        RoughSpecularIndirect[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        LightingValid[pixel] = 0.0;
        return;
    }

    uint2 gbufferA = GbufferAImage[screenCoord];
    float4 gbufferB = GbufferBImage[screenCoord];
    float4 gbufferC = GbufferCImage[screenCoord];
    float4 gbufferD = GbufferDImage[screenCoord];

    float sceneDepth = screenProbeLinearDepth(deviceZ, uViewData);
    float2 screenUV = (float2(screenCoord) + 0.5) * uViewData.resolution.zw;
    float3 viewPosition = screenProbeViewPositionAtDepth(screenUV, sceneDepth, uViewData);
    float3 viewDirWS = mul(uViewData.worldToViewRot, normalize(-viewPosition));
    ShadingParams sp = decodeShadingParams(gbufferA, gbufferB, gbufferC, gbufferD, viewDirWS);

    // UE 探针权重挑选和辐照度采样全程用清漆顶层法线(Material.WorldNormal), 只有粗糙镜面循环
    // 内部单独换成底层法线(LumenScreenProbeGather.usf:1524 在这之后才重新赋值)——非清漆像素
    // clearCoatNormalWS == normalWS, 这里统一用它不影响非清漆结果
    float3 normalWS = sp.clearCoatNormalWS;
    float3 normalVS = mul(normalWS, uViewData.worldToViewRot);

    float2 noiseOffset = float2(0.0, 0.0);
    {
        float effectiveJitterWidth = kFullResolutionJitterWidth
            * lerp(1.0, 0.5, saturate((sceneDepth - kJitterWidthFadeStartDepth) / kJitterWidthFadeLength));
        float2 jitterE = getRadiosityProbeTexelCenter(BlueNoiseVec2Texture, screenProbeFrameIndex(), screenCoord);
        float2 jitterNoiseOffset = (jitterE * 2.0 - 1.0) * float(kScreenProbeDownsampleFactor) * effectiveJitterWidth;

        float2 jitteredCoord = clamp(
            float2(screenCoord) + jitterNoiseOffset,
            float2(PC.viewRectMin),
            float2(PC.viewRectMin + PC.viewExtent) - 1.0);
        float jitteredDepth = screenProbeLinearDepth(SceneDepthImage[uint2(jitteredCoord)].r, uViewData);
        float3 jitteredPosition = screenProbeViewPositionAtDepth(jitteredCoord * uViewData.resolution.zw, jitteredDepth, uViewData);

        float relativeDepthDifference = calculatePlaneDistance(viewPosition, normalVS, jitteredPosition) / sceneDepth;
        float depthWeight = exp2(-1000000.0 * relativeDepthDifference * relativeDepthDifference);
        if (depthWeight > 0.01)
            noiseOffset = jitterNoiseOffset;
    }

    ScreenProbeUpsampleWeights probeSample = calculateUpsampleWeights(
        ScreenProbeSceneDepth,
        AdaptiveScreenProbePosAndTileIndices,
        AdaptiveScreenProbeTotalAndTileCounts,
        float2(screenCoord),
        noiseOffset,
        PC.viewRectMin,
        PC.viewExtent,
        PC.tileGridSize,
        PC.atlasViewSize,
        PC.maxNumAdaptiveProbes,
        screenProbeFrameIndex(),
        viewPosition,
        normalVS,
        sceneDepth,
        uViewData);

    float4 weights = probeSample.weights;
    bool lightingIsValid = dot(weights, float4(1.0)) >= kMinProbeInterpolationWeight;
    if (!lightingIsValid)
    {
        weights = probeSample.fallbackWeights;
        lightingIsValid = dot(weights, float4(1.0)) >= kMinProbeInterpolationWeight;
    }
    weights = lightingIsValid ? weights / max(dot(weights, float4(1.0)), kMinProbeInterpolationWeight) : float4(0.0);

#if SCREEN_PROBE_IRRADIANCE_FORMAT == SCREEN_PROBE_IRRADIANCE_FORMAT_SH3
    ThreeBandSHRGB radianceSH;
    radianceSH.r = zeroSH3();
    radianceSH.g = zeroSH3();
    radianceSH.b = zeroSH3();
    for (uint cornerIndex = 0; cornerIndex < 4; ++cornerIndex)
    {
        if (weights[cornerIndex] > 0.0)
        {
            ThreeBandSHRGB probeSH = readScreenProbeRadianceSH(
                ScreenProbeRadianceSHAmbient, ScreenProbeRadianceSHDirectional, probeSample.atlasCoord[cornerIndex], PC.atlasViewSize.x);
            radianceSH = addSH3(radianceSH, scaleSH3(probeSH, weights[cornerIndex]));
        }
    }
    float3 irradiance = lightingIsValid
        ? evaluateScreenProbeSHIrradiance(radianceSH, normalWS)
        : float3(0.0);
#else
    float2 irradianceUV = inverseEquiAreaSphericalMapping(normalWS);
    float2 irradianceCoord = irradianceUV * float(kScreenProbeIrradianceResolution) + 1.0;
    float3 irradiance = float3(0.0);
    for (uint cornerIndex = 0; cornerIndex < 4; ++cornerIndex)
    {
        if (weights[cornerIndex] > 0.0)
        {
            float2 atlasUV = (float2(probeSample.atlasCoord[cornerIndex] * kScreenProbeIrradianceWithBorderResolution) + irradianceCoord)
                / float2(PC.atlasViewSize * kScreenProbeIrradianceWithBorderResolution);
            irradiance += ScreenProbeIrradianceWithBorder.SampleLevel(LinearSampler, atlasUV, 0.0).rgb * weights[cornerIndex];
        }
    }
#endif

    float3 diffuseIndirect = irradiance / PI;
    float3 roughSpecularIndirect = integrateRoughSpecular(sp, diffuseIndirect, probeSample, weights, lightingIsValid, screenCoord);

    float e = getRadiosityProbeTexelCenter(BlueNoiseVec2Texture, screenProbeFrameIndex() % 8u, screenCoord).x;
    DiffuseIndirect[pixel] = float4(quantizeFloatColor(diffuseIndirect, kR11G11B10QuantizationError, e), 1.0);
    RoughSpecularIndirect[pixel] = float4(quantizeFloatColor(roughSpecularIndirect, kR11G11B10QuantizationError, e), 1.0);
    LightingValid[pixel] = lightingIsValid ? 1.0 : 0.0;
}
