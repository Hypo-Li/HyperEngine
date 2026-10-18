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
Texture2D<float> DownsampledSceneDepth;

[[vk::binding(4, 2)]]
Texture2D<float4> DownsampledWorldNormal;

[[vk::binding(5, 2)]]
Texture2D<float4> BlueNoiseVec2Texture;

[[vk::binding(6, 2)]]
[format("r8ui")]
RWTexture2D<uint> StochasticOffset;

struct ScreenProbeShortRangeAOUpsampleWeightsPushConstants
{
    uint2 viewRectMin;
    uint2 viewExtent;
    uint2 downsampledViewExtent;
    uint2 padding0;
};
[[vk::push_constant]]
ScreenProbeShortRangeAOUpsampleWeightsPushConstants PC;

static const float kUpsampleDepthWeightScale = -5000.0;

static const uint2 kCornerOffsets[4] = { uint2(0, 0), uint2(1, 0), uint2(0, 1), uint2(1, 1) };

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;
    if (any(pixel >= PC.viewExtent))
        return;

    uint2 screenCoord = pixel + PC.viewRectMin;

    float deviceZ = SceneDepthImage[screenCoord].r;
    ShadingModel shadingModel;
    bool hasAnisotropy;
    unpackShadingModelByte(GbufferBImage[screenCoord].w, shadingModel, hasAnisotropy);
    if (deviceZ == 0.0 || shadingModel == ShadingModel::Unlit)
    {
        StochasticOffset[pixel] = 0u;
        return;
    }

    float3 normalWS = decodeGBufferNormal(GbufferAImage[screenCoord].r);
    float3 normalVS = mul(normalWS, uViewData.worldToViewRot);
    float sceneDepth = screenProbeLinearDepth(deviceZ, uViewData);
    float2 screenUV = (float2(screenCoord) + 0.5) * uViewData.resolution.zw;
    float3 viewPosition = screenProbeViewPositionAtDepth(screenUV, sceneDepth, uViewData);

    uint2 viewMin2 = PC.viewRectMin / kShortRangeAODownsampleFactor;
    int2 anchor = shortRangeAODownsampleAnchor(int2(screenCoord));

    float4 weights = float4(0.0, 0.0, 0.0, 0.0);
    for (uint cornerIndex = 0; cornerIndex < 4u; ++cornerIndex)
    {
        uint2 candidateGlobal = uint2(anchor) + kCornerOffsets[cornerIndex];
        bool candidateValid = all(candidateGlobal >= viewMin2) && all(candidateGlobal < viewMin2 + PC.downsampledViewExtent);
        if (!candidateValid)
            continue;

        uint2 candidateLocal = candidateGlobal - viewMin2;
        float cornerDepth = DownsampledSceneDepth[candidateLocal];
        if (cornerDepth <= 0.0)
            continue;

        uint2 jitter = getShortRangeAODownsampleJitter(candidateGlobal, screenProbeFrameIndex());
        uint2 sourceFull = candidateGlobal * kShortRangeAODownsampleFactor + jitter;
        int2 delta = int2(sourceFull) - int2(screenCoord);

        float triangleWeight = max(0.0, 2.0 - abs(float(delta.x))) * max(0.0, 2.0 - abs(float(delta.y)));
        if (triangleWeight <= 0.0)
            continue;

        float2 cornerUV = (float2(sourceFull) + 0.5) * uViewData.resolution.zw;
        float3 cornerViewPos = screenProbeViewPositionAtDepth(cornerUV, cornerDepth, uViewData);
        float planeDistance = calculatePlaneDistance(viewPosition, normalVS, cornerViewPos);
        float relativeDepthDifference = planeDistance / max(sceneDepth, 1e-4);
        float depthWeight = exp2(kUpsampleDepthWeightScale * relativeDepthDifference * relativeDepthDifference);

        float3 cornerNormalWS = normalize(DownsampledWorldNormal[candidateLocal].xyz * 2.0 - 1.0);
        float normalWeight = shortRangeAONormalWeight(normalWS, cornerNormalWS);

        weights[cornerIndex] = triangleWeight * depthWeight * normalWeight;
    }

    float totalWeight = dot(weights, float4(1.0, 1.0, 1.0, 1.0));
    bool canReconstruct = totalWeight >= 0.01;

    uint chosenIndex = 0u;
    if (canReconstruct)
    {
        float randomScalar = shortRangeAOBlueNoiseScalar(BlueNoiseVec2Texture, screenCoord, screenProbeFrameIndex()) * totalWeight;
        float3 prefixSum = float3(weights.x, weights.x + weights.y, weights.x + weights.y + weights.z);
        if (randomScalar <= prefixSum.x && weights.x > 0.0)
            chosenIndex = 0u;
        else if (randomScalar <= prefixSum.y && weights.y > 0.0)
            chosenIndex = 1u;
        else if (randomScalar <= prefixSum.z && weights.z > 0.0)
            chosenIndex = 2u;
        else
            chosenIndex = 3u;
    }

    uint2 chosenOffset = kCornerOffsets[chosenIndex];
    uint packed = (chosenOffset.x & 1u) | ((chosenOffset.y & 1u) << 1u) | (canReconstruct ? 4u : 0u);
    StochasticOffset[pixel] = packed;
}
