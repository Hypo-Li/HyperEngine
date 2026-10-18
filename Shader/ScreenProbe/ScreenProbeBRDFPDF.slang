// 对应 UE ScreenProbeGatherScreenDataCS 的默认路径(BRDF_PDF_SPHERICAL_HARMONIC=1,
// LumenScreenProbeImportanceSamplingShared.ush:8), 不做 ScreenProbeDisocclusion(M4 已确认的偏差)、
// 不做 Foliage 双面材质特殊分支(ScreenProbeCommon.slang:11 已确认的偏差), 见
// Document/Lumen_ScreenProbeGather_Plan.md §9、M7.1。
#include "../Common/Binding.slang"
#include "../Common/GBufferEncoding.slang"
#include "../Common/SHCommon.slang"
#include "ScreenProbeCommon.slang"

[[vk::binding(0, 2)]]
DepthTexture2D SceneDepth;

[[vk::binding(1, 2)]]
Texture2D<uint2> GbufferAImage;

[[vk::binding(2, 2)]]
Texture2D<float4> GbufferBImage;

[[vk::binding(3, 2)]]
Texture2D<float> ScreenProbeSceneDepth;

[[vk::binding(4, 2)]]
StructuredBuffer<uint> AdaptiveScreenProbePosAndTileIndices;

[[vk::binding(5, 2)]]
StructuredBuffer<uint> AdaptiveScreenProbeTotalAndTileCounts;

// UE 用 RWBuffer<float>(不是结构体数组), 每探针连续 9 个系数——跟 ThreeBandSH 的
// v0(4)+v1(4)+v2(1) 对应, 避免结构体数组跨后端 padding 不一致
[[vk::binding(6, 2)]]
RWStructuredBuffer<float> BRDFProbabilityDensityFunctionSH;

struct ScreenProbeBRDFPDFPushConstants
{
    uint2 viewRectMin;
    uint2 viewExtent;
    uint atlasWidth;
    uint numUniformProbes;
    uint maxNumAdaptiveProbes;
};
[[vk::push_constant]]
ScreenProbeBRDFPDFPushConstants PC;

static const uint kNumSHCoefficients = 9;

groupshared float4 sharedV0[64];
groupshared float4 sharedV1[64];
groupshared float sharedV2[64];
groupshared float sharedValid[64];

// numthreads 要求字面量, 跟 8x8 手动保持一致(等于 kScreenProbeTracingOctahedronResolution,
// 同时也是 UE PROBE_THREADGROUP_SIZE_2D 的固定值)
[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    uint2 atlasCoord = groupId.xy;
    uint probeIndex = atlasCoord.y * PC.atlasWidth + atlasCoord.x;
    uint threadIndex = groupThreadId.y * 8u + groupThreadId.x;
    bool isCenterSample = all(groupThreadId.xy == uint2(4, 4));

    uint numProbes = getNumScreenProbes(PC.numUniformProbes, loadNumAdaptiveScreenProbes(AdaptiveScreenProbeTotalAndTileCounts), PC.maxNumAdaptiveProbes);
    bool probeValid = probeIndex < numProbes && atlasCoord.x < PC.atlasWidth;
    float probeDepth = probeValid ? ScreenProbeSceneDepth[atlasCoord] : 0.0;

    ThreeBandSH sh = zeroSH3();
    bool pixelValid = false;

    if (probeValid && probeDepth > 0.0)
    {
        uint2 screenPosition = getScreenProbeScreenPosition(
            probeIndex, atlasCoord, PC.numUniformProbes, PC.viewRectMin, PC.viewExtent, screenProbeFrameIndex(), AdaptiveScreenProbePosAndTileIndices);
        float2 probeScreenUV = (float2(screenPosition) + 0.5) * uViewData.resolution.zw;

        // 探针间距(kScreenProbeDownsampleFactor)就是探针的插值 footprint 半径, 8x8 采样正好铺满
        float2 sampleOffsetPixels = ((float2(groupThreadId.xy) + 0.5) / 8.0 * 2.0 - 1.0) * float(kScreenProbeDownsampleFactor);
        if (isCenterSample)
            sampleOffsetPixels = float2(0.0, 0.0);

        float2 pixelScreenUV = probeScreenUV + sampleOffsetPixels * uViewData.resolution.zw;
        float2 minUV = float2(PC.viewRectMin) * uViewData.resolution.zw;
        float2 maxUV = float2(PC.viewRectMin + PC.viewExtent - 1u) * uViewData.resolution.zw;
        pixelScreenUV = clamp(pixelScreenUV, minUV, maxUV);
        uint2 pixelCoord = uint2(pixelScreenUV * uViewData.resolution.xy);

        float deviceZ = SceneDepth[pixelCoord].r;
        ShadingModel shadingModel;
        bool hasAnisotropy;
        unpackShadingModelByte(GbufferBImage[pixelCoord].w, shadingModel, hasAnisotropy);

        if (deviceZ != 0.0 && shadingModel != ShadingModel::Unlit)
        {
            float3 normalWS = decodeGBufferNormal(GbufferAImage[pixelCoord].r);
            float3 normalVS = mul(normalWS, uViewData.worldToViewRot);

            float pixelLinearDepth = screenProbeLinearDepth(deviceZ, uViewData);
            float3 pixelViewPosition = screenProbeViewPositionAtDepth(pixelScreenUV, pixelLinearDepth, uViewData);
            float3 probeViewPosition = screenProbeViewPositionAtDepth(probeScreenUV, probeDepth, uViewData);

            // 平面距离测试全程在 view space 算(UE 用 translated world space): 两者只差一个刚体变换,
            // 点积/距离不变, 数值结果一样
            float planeDistance = abs(dot(probeViewPosition - pixelViewPosition, normalVS));
            float relativeDepthDifference = planeDistance / probeDepth;
            float depthWeight = exp2(kScreenProbeInterpolationDepthWeight * relativeDepthDifference * relativeDepthDifference);

            if (depthWeight > 0.01 || isCenterSample)
            {
                sh = calcDiffuseTransferSH3(normalWS);
                pixelValid = true;
            }
        }
    }

    sharedV0[threadIndex] = sh.v0;
    sharedV1[threadIndex] = sh.v1;
    sharedV2[threadIndex] = sh.v2;
    sharedValid[threadIndex] = pixelValid ? 1.0 : 0.0;

    GroupMemoryBarrierWithGroupSync();

    // 64 个元素的规约, 单线程串行做——跟 ScreenProbeCalculateMoving.slang 同一个套路
    if (threadIndex == 0 && probeValid)
    {
        ThreeBandSH sum = zeroSH3();
        float count = 0.0;
        for (uint i = 0; i < 64u; ++i)
        {
            sum.v0 += sharedV0[i];
            sum.v1 += sharedV1[i];
            sum.v2 += sharedV2[i];
            count += sharedValid[i];
        }

        if (count > 0.0)
        {
            float invCount = 1.0 / count;
            sum.v0 *= invCount;
            sum.v1 *= invCount;
            sum.v2 *= invCount;
        }

        uint baseOffset = probeIndex * kNumSHCoefficients;
        BRDFProbabilityDensityFunctionSH[baseOffset + 0] = sum.v0.x;
        BRDFProbabilityDensityFunctionSH[baseOffset + 1] = sum.v0.y;
        BRDFProbabilityDensityFunctionSH[baseOffset + 2] = sum.v0.z;
        BRDFProbabilityDensityFunctionSH[baseOffset + 3] = sum.v0.w;
        BRDFProbabilityDensityFunctionSH[baseOffset + 4] = sum.v1.x;
        BRDFProbabilityDensityFunctionSH[baseOffset + 5] = sum.v1.y;
        BRDFProbabilityDensityFunctionSH[baseOffset + 6] = sum.v1.z;
        BRDFProbabilityDensityFunctionSH[baseOffset + 7] = sum.v1.w;
        BRDFProbabilityDensityFunctionSH[baseOffset + 8] = sum.v2;
    }
}
