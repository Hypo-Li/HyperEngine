// 对应 UE ScreenProbeConvertToIrradianceCS 的两种辐照度格式、wave 分支(LumenScreenProbeFiltering.usf:598-998), 见 Document/Lumen_ScreenProbeGather_Plan.md §6.5。
#include "../Common/Binding.slang"
#include "../Common/SHCommon.slang"
#include "ScreenProbeCommon.slang"
#include "ScreenProbeIrradianceFormat.slang"
#include "ScreenProbeIrradianceSH.slang"

[[vk::binding(0, 2)]]
Texture2D<float> ScreenProbeSceneDepth;

[[vk::binding(1, 2)]]
StructuredBuffer<uint> AdaptiveScreenProbeTotalAndTileCounts;

[[vk::binding(2, 2)]]
Texture2D<float4> ScreenProbeRadiance;

#if SCREEN_PROBE_IRRADIANCE_FORMAT == SCREEN_PROBE_IRRADIANCE_FORMAT_SH3

[[vk::binding(3, 2)]]
[format("r11f_g11f_b10f")]
RWTexture2D<float4> ScreenProbeRadianceSHAmbient;

[[vk::binding(4, 2)]]
[format("rgba16f")]
RWTexture2D<float4> ScreenProbeRadianceSHDirectional;

#else

[[vk::binding(3, 2)]]
[format("r11f_g11f_b10f")]
RWTexture2D<float4> ScreenProbeIrradianceWithBorder;

#endif

struct ScreenProbeConvertToIrradiancePushConstants
{
    uint atlasWidth;
    uint numUniformProbes;
    uint maxNumAdaptiveProbes;
};
[[vk::push_constant]]
ScreenProbeConvertToIrradiancePushConstants PC;

static const uint kTexelCount = kScreenProbeGatherOctahedronResolution * kScreenProbeGatherOctahedronResolution;

groupshared ThreeBandSHRGB SharedWaveSumRadianceSH[kTexelCount];

// numthreads 必须同时等于 kScreenProbeGatherOctahedronResolution 和 kScreenProbeIrradianceWithBorderResolution
[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    uint2 atlasCoord = groupId.xy;
    uint2 texelCoord = groupThreadId.xy;
    uint probeIndex = atlasCoord.y * PC.atlasWidth + atlasCoord.x;

    uint numProbes = getNumScreenProbes(PC.numUniformProbes, loadNumAdaptiveScreenProbes(AdaptiveScreenProbeTotalAndTileCounts), PC.maxNumAdaptiveProbes);
    if (probeIndex >= numProbes || atlasCoord.x >= PC.atlasWidth)
        return;

    uint threadIndex = texelCoord.y * kScreenProbeGatherOctahedronResolution + texelCoord.x;

#if SCREEN_PROBE_IRRADIANCE_FORMAT != SCREEN_PROBE_IRRADIANCE_FORMAT_SH3
    uint2 irradianceAtlasCoord = atlasCoord * kScreenProbeIrradianceWithBorderResolution + texelCoord;
#endif

    if (!(ScreenProbeSceneDepth[atlasCoord] > 0.0))
    {
#if SCREEN_PROBE_IRRADIANCE_FORMAT == SCREEN_PROBE_IRRADIANCE_FORMAT_SH3
        if (threadIndex == 0u)
            ScreenProbeRadianceSHAmbient[atlasCoord] = float4(0.0, 0.0, 0.0, 1.0);
        if (threadIndex < 3u)
        {
            ScreenProbeRadianceSHDirectional[screenProbeRadianceSHDirectionalCoord(atlasCoord, PC.atlasWidth, threadIndex * 2u + 0u)] = float4(0.0);
            ScreenProbeRadianceSHDirectional[screenProbeRadianceSHDirectionalCoord(atlasCoord, PC.atlasWidth, threadIndex * 2u + 1u)] = float4(0.0);
        }
#else
        ScreenProbeIrradianceWithBorder[irradianceAtlasCoord] = float4(0.0, 0.0, 0.0, 1.0);
#endif
        return;
    }

    float2 radianceUV = (float2(texelCoord) + 0.5) / float(kScreenProbeGatherOctahedronResolution);
    ThreeBandSH basis = shBasisFunction3(equiAreaSphericalMapping(radianceUV));
    float3 radiance = ScreenProbeRadiance[atlasCoord * kScreenProbeGatherOctahedronResolution + texelCoord].rgb;
    ThreeBandSHRGB lightingSH = mulSH3(basis, radiance);

    float invNumSamples = 1.0 / float(kTexelCount);
    ThreeBandSHRGB waveSH;
    waveSH.r.v0 = WaveActiveSum(lightingSH.r.v0) * invNumSamples;
    waveSH.g.v0 = WaveActiveSum(lightingSH.g.v0) * invNumSamples;
    waveSH.b.v0 = WaveActiveSum(lightingSH.b.v0) * invNumSamples;
    waveSH.r.v1 = WaveActiveSum(lightingSH.r.v1) * invNumSamples;
    waveSH.g.v1 = WaveActiveSum(lightingSH.g.v1) * invNumSamples;
    waveSH.b.v1 = WaveActiveSum(lightingSH.b.v1) * invNumSamples;
    waveSH.r.v2 = WaveActiveSum(lightingSH.r.v2) * invNumSamples;
    waveSH.g.v2 = WaveActiveSum(lightingSH.g.v2) * invNumSamples;
    waveSH.b.v2 = WaveActiveSum(lightingSH.b.v2) * invNumSamples;

    uint laneCount = WaveGetLaneCount();
    // 假设同一个 wave 的 lane 对应连续的线性线程序号, UE 的 wave 分支同样这么假设
    if (WaveIsFirstLane())
        SharedWaveSumRadianceSH[threadIndex / laneCount] = waveSH;
    GroupMemoryBarrierWithGroupSync();

    uint numWaves = (kTexelCount + laneCount - 1u) / laneCount;
    ThreeBandSHRGB radianceSH = SharedWaveSumRadianceSH[0];
    for (uint waveIndex = 1; waveIndex < numWaves; ++waveIndex)
        radianceSH = addSH3(radianceSH, SharedWaveSumRadianceSH[waveIndex]);

#if SCREEN_PROBE_IRRADIANCE_FORMAT == SCREEN_PROBE_IRRADIANCE_FORMAT_SH3
    if (threadIndex == 0u)
    {
        float3 ambient = screenProbeRadianceSHAmbient(radianceSH);
        ScreenProbeRadianceSHAmbient[atlasCoord] = float4(
            quantizeForFloatRenderTarget(ambient, int3(int2(atlasCoord), int(screenProbeFrameIndex() % 8u))), 1.0);
    }

    if (threadIndex < 3u)
    {
        float4 sh0;
        float4 sh1;
        screenProbeRadianceSHDirectional(radianceSH, threadIndex, sh0, sh1);
        ScreenProbeRadianceSHDirectional[screenProbeRadianceSHDirectionalCoord(atlasCoord, PC.atlasWidth, threadIndex * 2u + 0u)] = sh0;
        ScreenProbeRadianceSHDirectional[screenProbeRadianceSHDirectionalCoord(atlasCoord, PC.atlasWidth, threadIndex * 2u + 1u)] = sh1;
    }
#else
    uint2 wrappedTexelCoord = octahedralMapWrapBorder(texelCoord, kScreenProbeIrradianceWithBorderResolution, 1u);
    float2 irradianceUV = (float2(wrappedTexelCoord) + 0.5) / float(kScreenProbeIrradianceResolution);
    float3 texelDirection = equiAreaSphericalMapping(irradianceUV);
    float3 irradiance = 4.0 * PI * dotSH3(radianceSH, calcDiffuseTransferSH3(texelDirection));

    ScreenProbeIrradianceWithBorder[irradianceAtlasCoord] = float4(
        quantizeForFloatRenderTarget(irradiance, int3(int2(irradianceAtlasCoord), int(screenProbeFrameIndex() % 8u))), 1.0);
#endif
}
