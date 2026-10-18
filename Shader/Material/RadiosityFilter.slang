// Radiosity [B] SpatialFilter —— 对 [A] Trace 写的 TraceRadianceAtlas 做探针级 4 邻域(上下
// 左右)空间滤波: 中心权重 2 + 每个通过 calculatePlaneWeight 的邻居权重 1, 写进
// FilteredTraceRadianceAtlas 给 [C] ConvertToSH 用。按探针 dispatch(每页 32×32 个探针, 不是
// 128×128 个 texel), 跨页寻址跟 [D] Integrate 的 sampleProbeSH 同一套逻辑, 只是这里换算到
// texel 坐标去采 TraceRadianceAtlas, 不是探针坐标去采 ProbeSH。跟 UE
// LumenRadiositySpatialFilterProbeRadiance(LumenRadiosity.usf)对照, 见
// Document/Lumen_Radiosity_Plan.md §3[B]。
#include "../Common/Binding.slang"
#include "../Common/CardTexelDecode.slang"
#include "../Common/FloatQuantization.slang"
#include "../Common/RadiosityProbeSampling.slang"

[[vk::binding(0, 3)]]
StructuredBuffer<InstanceData> sInstanceData;

[[vk::binding(0, 2)]]
Texture2D<float4> CardNormalAtlas;

[[vk::binding(1, 2)]]
Texture2D<float> CardDepthAtlas;

[[vk::binding(2, 2)]]
Texture2D<float4> TraceRadianceAtlas;

[[vk::binding(3, 2)]]
StructuredBuffer<uint> CardPages;

[[vk::binding(4, 2)]]
[format("r11f_g11f_b10f")]
RWTexture2D<float4> FilteredTraceRadianceAtlas;

// 跟 RadiosityIntegrateConstants(RadiosityIntegrate.slang)同一套页身份字段, 这个 pass 同样
// 需要 pageIndirectionOffset 做跨页寻址
struct RadiosityFilterConstants
{
    float cardCenterX, cardCenterY, cardCenterZ;
    float cardHalfExtentsX, cardHalfExtentsY, cardHalfExtentsZ;
    uint directionIndex;
    uint packedAddress; // mCardPageTable 打包值, 用 decodeCardPageAddress() 解出像素原点+实际尺寸
    uint pageIndexX, pageIndexY;
    uint pageCountX, pageCountY;
    uint pageIndirectionOffset;
    uint instanceIndex;
    uint temporalIndex; // 跟 Trace 同一个值
};
[[vk::push_constant]]
RadiosityFilterConstants PC;

static const uint kProbesPerPageAxis = uint(kSurfaceCachePageSize) / 4u;

// probeCoordSigned 是本页局部探针坐标, 允许越界——跨页换算/有效性判断跟
// RadiosityIntegrate.slang::sampleProbeSH 同一套逻辑; 这里不需要邻居的 SH 值, 只需要它在
// TraceRadianceAtlas 里对应 4x4 texel 块的绝对图集坐标, 通过了 calculatePlaneWeight 才返回 true。
// currentPixelOrigin/currentPixelExtent: 当前这个探针所在页的实际像素原点+尺寸——子页分配
// 的卡这个尺寸可能远小于整页, 下面按"这个探针坐标是否落在当前卡自己的有效探针范围内"分流,
// 理由跟 RadiosityIntegrate.slang::sampleProbeSH() 的同名分支完全一致(同一个 bug、同一处修复,
// 那边的注释有完整解释)
bool resolveFilterNeighborProbe(int2 probeCoordSigned, WorldCoord referenceWorldPos, float3 referenceNormalWS, int2 currentPixelOrigin, int2 currentPixelExtent, out int2 neighborTraceTexelOrigin)
{
    neighborTraceTexelOrigin = int2(0, 0);

    int2 currentProbeExtent = currentPixelExtent / 4;
    bool withinCurrentCard = all(probeCoordSigned >= int2(0, 0)) && all(probeCoordSigned < currentProbeExtent);

    int2 probeAtlasCoord;
    int2 neighborPagePixelOrigin;
    uint2 neighborPageIndex;
    uint2 neighborPageExtent;

    if (withinCurrentCard)
    {
        probeAtlasCoord = currentPixelOrigin / 4 + probeCoordSigned;
        neighborPagePixelOrigin = currentPixelOrigin;
        neighborPageIndex = uint2(PC.pageIndexX, PC.pageIndexY);
        neighborPageExtent = uint2(currentPixelExtent);
    }
    else
    {
        if (PC.pageCountX <= 1u && PC.pageCountY <= 1u)
            return false; // 单页卡(含子页分配)越出自己的有效范围就是真实几何边缘, 没有邻居

        bool found = resolveCrossPageAtlasCoord(
            probeCoordSigned, int(kProbesPerPageAxis),
            uint2(PC.pageIndexX, PC.pageIndexY), uint2(PC.pageCountX, PC.pageCountY),
            PC.pageIndirectionOffset, CardPages,
            probeAtlasCoord);
        if (!found)
            return false;

        // 走到这个分支说明 pageCount>1, 邻居页恒为整页(多页卡不会子页分配)
        int2 wrappedLocal = ((probeCoordSigned % int(kProbesPerPageAxis)) + int(kProbesPerPageAxis)) % int(kProbesPerPageAxis);
        neighborPagePixelOrigin = (probeAtlasCoord - wrappedLocal) * 4;
        int2 pageDelta = int2(
            probeCoordSigned.x < 0 ? -1 : (probeCoordSigned.x >= int(kProbesPerPageAxis) ? 1 : 0),
            probeCoordSigned.y < 0 ? -1 : (probeCoordSigned.y >= int(kProbesPerPageAxis) ? 1 : 0));
        neighborPageIndex = uint2(int2(PC.pageIndexX, PC.pageIndexY) + pageDelta);
        neighborPageExtent = uint2(kSurfaceCachePageSize, kSurfaceCachePageSize);
    }

    int2 wrappedLocalForJitter = probeAtlasCoord - neighborPagePixelOrigin / 4;
    uint neighborTemporalIndex = PC.temporalIndex;
    uint2 neighborProbeTexel = uint2(wrappedLocalForJitter) * 4 + getRadiosityProbeJitter(neighborTemporalIndex);

    CardTexelInfo neighborInfo = decodeCardTexel(
        neighborProbeTexel,
        neighborPageIndex, uint2(PC.pageCountX, PC.pageCountY),
        uint2(neighborPagePixelOrigin), neighborPageExtent, PC.directionIndex,
        float3(PC.cardCenterX, PC.cardCenterY, PC.cardCenterZ),
        float3(PC.cardHalfExtentsX, PC.cardHalfExtentsY, PC.cardHalfExtentsZ),
        PC.instanceIndex,
        CardDepthAtlas, CardNormalAtlas);
    if (!neighborInfo.valid)
        return false;

    float3 neighborWorldPosRel = calculateRelativeWorldPosition(neighborInfo.worldPos, referenceWorldPos);
    if (!calculatePlaneWeight(float3(0, 0, 0), referenceNormalWS, neighborWorldPosRel))
        return false;

    neighborTraceTexelOrigin = probeAtlasCoord * 4;
    return true;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 groupId : SV_GroupID, uint3 localId : SV_GroupThreadID)
{
    CardPageRegion pageRegion = decodeCardPageAddress(PC.packedAddress);
    uint2 probesInPage = uint2(pageRegion.pixelExtent) / 4u;

    uint2 probeCoord = groupId.xy * 8 + localId.xy;
    if (probeCoord.x >= probesInPage.x || probeCoord.y >= probesInPage.y)
        return;

    uint temporalIndex = PC.temporalIndex;
    uint2 probeJitter = getRadiosityProbeJitter(temporalIndex);
    uint2 probeTexel = probeCoord * 4 + probeJitter;

    CardTexelInfo probeInfo = decodeCardTexel(
        probeTexel,
        uint2(PC.pageIndexX, PC.pageIndexY),
        uint2(PC.pageCountX, PC.pageCountY),
        uint2(pageRegion.pixelOrigin),
        uint2(pageRegion.pixelExtent),
        PC.directionIndex,
        float3(PC.cardCenterX, PC.cardCenterY, PC.cardCenterZ),
        float3(PC.cardHalfExtentsX, PC.cardHalfExtentsY, PC.cardHalfExtentsZ),
        PC.instanceIndex,
        CardDepthAtlas,
        CardNormalAtlas);

    int2 centerTexelOrigin = pageRegion.pixelOrigin + int2(probeCoord * 4);

    if (!probeInfo.valid)
    {
        for (uint y = 0; y < 4; ++y)
        {
            for (uint x = 0; x < 4; ++x)
                FilteredTraceRadianceAtlas[centerTexelOrigin + int2(x, y)] = float4(0, 0, 0, 1);
        }
        return;
    }

    int2 probeCoordSigned = int2(probeCoord);
    int2 neighborOffsets[4] = { int2(0, 1), int2(1, 0), int2(0, -1), int2(-1, 0) };

    const float kCenterWeight = 2.0;
    bool neighborValid[4];
    int2 neighborTexelOrigin[4];
    float totalWeight = kCenterWeight;
    for (int i = 0; i < 4; ++i)
    {
        neighborValid[i] = resolveFilterNeighborProbe(probeCoordSigned + neighborOffsets[i], probeInfo.worldPos, probeInfo.normalWS, pageRegion.pixelOrigin, pageRegion.pixelExtent, neighborTexelOrigin[i]);
        if (neighborValid[i])
            totalWeight += 1.0;
    }

    for (uint y = 0; y < 4; ++y)
    {
        for (uint x = 0; x < 4; ++x)
        {
            int2 rayTexel = int2(x, y);
            float3 radiance = TraceRadianceAtlas[centerTexelOrigin + rayTexel].rgb * kCenterWeight;
            for (int i = 0; i < 4; ++i)
            {
                if (neighborValid[i])
                    radiance += TraceRadianceAtlas[neighborTexelOrigin[i] + rayTexel].rgb;
            }
            int2 filteredTexelCoord = centerTexelOrigin + rayTexel;
            FilteredTraceRadianceAtlas[filteredTexelCoord] = float4(
                quantizeForFloatRenderTarget(radiance / totalWeight, int3(filteredTexelCoord, int(temporalIndex + 1u))), 1.0);
        }
    }
}
