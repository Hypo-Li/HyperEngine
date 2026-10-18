// 对应 UE ScreenProbeFilterGatherTracesCS(LumenScreenProbeFiltering.usf:299-506), 不做 Disocclusion 放宽角度权重
// 分支(没有对应的 ScreenProbeDisocclusion 数据, 见 Document/Lumen_ScreenProbeGather_Plan.md §6.9 差别 2)。
#include "../Common/Binding.slang"
#include "ScreenProbeCommon.slang"

[[vk::binding(0, 2)]]
Texture2D<float> ScreenProbeSceneDepth;

[[vk::binding(1, 2)]]
StructuredBuffer<uint> AdaptiveScreenProbePosAndTileIndices;

[[vk::binding(2, 2)]]
StructuredBuffer<uint> AdaptiveScreenProbeTotalAndTileCounts;

[[vk::binding(3, 2)]]
Texture2D<float4> ScreenProbeRadiance;

[[vk::binding(4, 2)]]
Texture2D<float> ScreenProbeHitDistance;

[[vk::binding(5, 2)]]
Texture2D<float> ScreenProbeMoving;

[[vk::binding(6, 2)]]
[format("r11f_g11f_b10f")]
RWTexture2D<float4> ScreenProbeRadianceFiltered;

struct ScreenProbeFilterGatherTracesPushConstants
{
    uint2 viewRectMin;
    uint2 viewExtent;
    uint2 tileGridSize;
    uint atlasWidth;
    uint numUniformProbes;
    uint maxNumAdaptiveProbes;
    float maxWorldTraceDistance;
};
[[vk::push_constant]]
ScreenProbeFilterGatherTracesPushConstants PC;

static const int2 kCrossOffsets[4] = { int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };
static const int2 kStrongFilterExtraOffsets[8] =
{
    int2(-2, 0), int2(2, 0), int2(0, -2), int2(0, 2),
    int2(-1, 1), int2(1, 1), int2(-1, -1), int2(1, -1)
};

// UE GetFilterPositionWeight(LumenScreenProbeFiltering.usf:292-297)
float filterPositionWeight(float neighborDepth, float sceneDepth)
{
    float relativeDepthDifference = abs(neighborDepth - sceneDepth) / sceneDepth;
    return neighborDepth > 0.0 ? exp2(-kSpatialFilterPositionWeightScale * relativeDepthDifference * relativeDepthDifference) : 0.0;
}

// 对应 UE GatherNeighborRadiance(LumenScreenProbeFiltering.usf:299-382), 邻居固定在统一探针网格上找
// (不搜索自适应探针, 跟 UE FILTER_SEARCH_ADAPTIVE_PROBES 默认关一致)
void gatherNeighborRadiance(
    int2 neighborTileCoord,
    uint2 texelCoord,
    float3 probeViewPosition,
    float3 coneDirectionVS,
    float sceneDepth,
    float ownHitDistance,
    bool relaxAngleWeight,
    in ViewData viewData,
    inout float3 totalRadiance,
    inout float totalWeight)
{
    if (any(neighborTileCoord < int2(0, 0)) || any(neighborTileCoord >= int2(PC.tileGridSize)))
        return;

    uint2 neighborAtlasCoord = uint2(neighborTileCoord);
    float neighborSceneDepth = ScreenProbeSceneDepth[neighborAtlasCoord];
    float positionWeight = filterPositionWeight(neighborSceneDepth, sceneDepth);
    if (positionWeight <= 0.0)
        return;

    uint2 neighborGatherTexelCoord = neighborAtlasCoord * kScreenProbeGatherOctahedronResolution + texelCoord;
    float neighborHitDistance = decodeProbeHitDistanceForFiltering(ScreenProbeHitDistance[neighborGatherTexelCoord], PC.maxWorldTraceDistance);
    if (neighborHitDistance < 0.0)
        return;

    float angleWeight = 1.0;
    if (!relaxAngleWeight)
    {
        // 邻居命中距离钳到自己的命中距离——防止邻居命中得比自己远时, 因为射线夹角算出来偏小
        // 而误把远处光照当作可信来源, 用来保留接触阴影(跟 UE 同一处注释)
        float clampedNeighborHitDistance = ownHitDistance >= 0.0 ? min(neighborHitDistance, ownHitDistance) : neighborHitDistance;

        uint2 neighborScreenPosition = getUniformScreenProbeScreenPosition(neighborAtlasCoord, PC.viewRectMin, screenProbeFrameIndex());
        float2 neighborUV = (float2(neighborScreenPosition) + 0.5) * viewData.resolution.zw;
        float3 neighborViewPosition = screenProbeViewPositionAtDepth(neighborUV, neighborSceneDepth, viewData);

        float3 neighborHitPosition = neighborViewPosition + coneDirectionVS * clampedNeighborHitDistance;
        float3 toNeighborHit = neighborHitPosition - probeViewPosition;
        float neighborAngle = acos(clamp(dot(normalize(toNeighborHit), coneDirectionVS), -1.0, 1.0));
        angleWeight = 1.0 - saturate(neighborAngle / kSpatialFilterMaxRadianceHitAngle);
    }

    float weight = positionWeight * angleWeight;
    totalRadiance += ScreenProbeRadiance[neighborGatherTexelCoord].rgb * weight;
    totalWeight += weight;
}

// numthreads 要求字面量, 跟 kScreenProbeGatherOctahedronResolution 手动保持一致
[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    uint2 atlasCoord = groupId.xy;
    uint2 texelCoord = groupThreadId.xy;
    uint probeIndex = atlasCoord.y * PC.atlasWidth + atlasCoord.x;

    uint numProbes = getNumScreenProbes(PC.numUniformProbes, loadNumAdaptiveScreenProbes(AdaptiveScreenProbeTotalAndTileCounts), PC.maxNumAdaptiveProbes);
    uint2 gatherTexelCoord = atlasCoord * kScreenProbeGatherOctahedronResolution + texelCoord;
    if (probeIndex >= numProbes || atlasCoord.x >= PC.atlasWidth)
        return;

    float probeDepth = ScreenProbeSceneDepth[atlasCoord];
    if (!(probeDepth > 0.0))
    {
        ScreenProbeRadianceFiltered[gatherTexelCoord] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    uint2 screenPosition = getScreenProbeScreenPosition(
        probeIndex, atlasCoord, PC.numUniformProbes, PC.viewRectMin, PC.viewExtent, screenProbeFrameIndex(), AdaptiveScreenProbePosAndTileIndices);
    uint2 screenTileCoord = getScreenTileCoord(screenPosition, PC.viewRectMin, screenProbeFrameIndex());
    float2 screenUV = (float2(screenPosition) + 0.5) * uViewData.resolution.zw;
    float3 probeViewPosition = screenProbeViewPositionAtDepth(screenUV, probeDepth, uViewData);

    float2 radianceUV = (float2(texelCoord) + 0.5) / float(kScreenProbeGatherOctahedronResolution);
    float3 coneDirectionVS = mul(equiAreaSphericalMapping(radianceUV), uViewData.worldToViewRot);

    float ownHitDistance = decodeProbeHitDistanceForFiltering(ScreenProbeHitDistance[gatherTexelCoord], PC.maxWorldTraceDistance);
    bool ownValid = ownHitDistance >= 0.0;

    float3 totalRadiance = ownValid ? ScreenProbeRadiance[gatherTexelCoord].rgb : float3(0.0, 0.0, 0.0);
    float totalWeight = ownValid ? 1.0 : 0.0;

    // UE 用探针被标记 moving 时放宽角度权重、同时扩核到 12-tap(LumenScreenProbeFiltering.usf:423-428)
    bool relaxAngleWeight = ScreenProbeMoving[atlasCoord] > 0.01;

    for (uint i = 0; i < 4; ++i)
    {
        gatherNeighborRadiance(
            int2(screenTileCoord) + kCrossOffsets[i], texelCoord, probeViewPosition, coneDirectionVS,
            probeDepth, ownHitDistance, relaxAngleWeight, uViewData, totalRadiance, totalWeight);
    }

    if (relaxAngleWeight)
    {
        for (uint i = 0; i < 8; ++i)
        {
            gatherNeighborRadiance(
                int2(screenTileCoord) + kStrongFilterExtraOffsets[i], texelCoord, probeViewPosition, coneDirectionVS,
                probeDepth, ownHitDistance, relaxAngleWeight, uViewData, totalRadiance, totalWeight);
        }
    }

    if (totalWeight > 0.0)
        totalRadiance /= totalWeight;

    ScreenProbeRadianceFiltered[gatherTexelCoord] = float4(
        quantizeForFloatRenderTarget(totalRadiance, int3(int2(gatherTexelCoord), int(screenProbeFrameIndex() % 8u))), 1.0);
}
