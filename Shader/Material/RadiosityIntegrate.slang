// Radiosity [D] Integrate —— 把 [C] ConvertToSH 写的 ProbeSH R/G/B 双线性插值(带外扩,
// 保证没有探针权重为 0)、跟这个 texel 自己的法线做余弦卷积点积, 得到辐照度, alpha=1/(1+N)
// 跟历史 IndirectLightingAtlas 时域混合, N 是这个 8×8 card tile 的累积帧数
// (RadiosityNumFramesAccumulatedAtlas, 新捕获页时清零, 跟 UE LumenRadiosityIntegrateCS 一致)。
// 按 texel dispatch(每页 128×128, 跟 card 图集同粒度)。跨页寻址、平面加权(calculatePlaneWeight)见 Document/Lumen_Radiosity_Plan.md §3[D]。
#include "../Common/Binding.slang"
#include "../Common/CardTexelDecode.slang"
#include "../Common/FloatQuantization.slang"
#include "../Common/RadiosityProbeSampling.slang"
#include "../Common/SHCommon.slang"

[[vk::binding(0, 3)]]
StructuredBuffer<InstanceData> sInstanceData;

[[vk::binding(0, 2)]]
Texture2D<float4> CardNormalAtlas;

[[vk::binding(1, 2)]]
Texture2D<float> CardDepthAtlas;

[[vk::binding(2, 2)]]
Texture2D<float4> ProbeSHRAtlas;

[[vk::binding(3, 2)]]
Texture2D<float4> ProbeSHGAtlas;

[[vk::binding(4, 2)]]
Texture2D<float4> ProbeSHBAtlas;

[[vk::binding(5, 2)]]
StructuredBuffer<uint> CardPages;

[[vk::binding(6, 2)]]
[format("r11f_g11f_b10f")]
RWTexture2D<float4> IndirectLightingAtlas;

[[vk::binding(7, 2)]]
[format("r8")]
RWTexture2D<float> RadiosityNumFramesAccumulatedAtlas;

// 跟 RadiosityTraceConstants(RadiosityTrace.slang)同一套页身份字段, 多了
// pageIndirectionOffset(跨页寻址、给邻居探针反解所在页要用), 少了 GDF 相关的部分
struct RadiosityIntegrateConstants
{
    float cardCenterX, cardCenterY, cardCenterZ;
    float cardHalfExtentsX, cardHalfExtentsY, cardHalfExtentsZ;
    uint directionIndex;
    uint packedAddress; // mCardPageTable 打包值, 用 decodeCardPageAddress() 解出像素原点+实际尺寸
    uint pageIndexX, pageIndexY;
    uint pageCountX, pageCountY;
    uint pageIndirectionOffset; // 这张卡在共享页表里的起始下标, 见 CardPageRange
    uint instanceIndex;
    uint temporalIndex; // 跟 Trace 同一个值
};
[[vk::push_constant]]
RadiosityIntegrateConstants PC;

groupshared float gNumFramesAccumulated;

static const uint kProbesPerPageAxis = uint(kSurfaceCachePageSize) / 4u;

// probeCoordSigned 是本页局部探针坐标系下的坐标, 允许越界(<0 或 >=32)。越界时经
// resolveCrossPageAtlasCoord 换算成邻页的探针图集坐标; 卡的真实几何边缘(没有邻页)或者
// 邻居 texel 本身没被捕获过, 返回 false, 调用方跳过这个探针, 不贡献权重——这是正确行为,
// 不是简化
// referenceWorldPos 是调用方(当前 texel)自己的世界坐标——邻居探针的世界坐标用
// calculateRelativeWorldPosition() 相对它取差, 不直接拿 WorldCoord 参与平面加权计算
// (那是 tile+offset 的大世界坐标表示, CalculatePlaneWeight 的点积/长度公式只对普通 float3
// 有意义)
// currentPixelOrigin/currentPixelExtent: 当前这个 texel 所在页的实际像素原点+尺寸(即
// decodeCardPageAddress() 解出来的那份)——子页分配的卡这个尺寸可能远小于整页, resolveCrossPageAtlasCoord()
// 那套"越界阈值固定用 kProbesPerPageAxis(32)"的判断只对"这一页本来就是整页"成立;
// 子页卡如果直接套用会把"其实已经超出我自己这张卡的有效范围, 但数值上还没到 32"的探针
// 坐标误判成"还在本页内、合法", 进而用 gridCoord*32 这个假设"整页起点"的公式算出一个跟
// 这张卡真实子页偏移完全对不上的坐标, 读到别的卡的数据或者垃圾值——这正是子页分配上线后
// 间接光几乎全黑的根因。修法: 先按"这个探针坐标是否落在当前卡自己的有效探针范围内"分流,
// 落在范围内直接用当前卡已知的 origin/extent, 不经过 resolveCrossPageAtlasCoord; 落在范围外
// 只有多页卡(pageCount>1, 这类卡不会走子页分配, 每页恒为整页)才可能是真正的跨页邻居,
// 单页卡(不管是不是子页)越界就是卡的真实几何边缘, 直接判定没有邻居
bool sampleProbeSH(int2 probeCoordSigned, WorldCoord referenceWorldPos, int2 currentPixelOrigin, int2 currentPixelExtent, out float4 shR, out float4 shG, out float4 shB, out float3 probeWorldPosRel)
{
    shR = float4(0, 0, 0, 0);
    shG = float4(0, 0, 0, 0);
    shB = float4(0, 0, 0, 0);
    probeWorldPosRel = float3(0, 0, 0);

    int2 currentProbeExtent = currentPixelExtent / 4;
    bool withinCurrentCard = all(probeCoordSigned >= int2(0, 0)) && all(probeCoordSigned < currentProbeExtent);

    int2 probeAtlasCoord;
    int2 neighborPixelOrigin;
    uint2 neighborPageIndex;
    uint2 neighborPageExtent;

    if (withinCurrentCard)
    {
        probeAtlasCoord = currentPixelOrigin / 4 + probeCoordSigned;
        neighborPixelOrigin = currentPixelOrigin;
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

        // wrappedLocal: 目标探针在它实际所在那一页里的本地坐标([0,32)), probeAtlasCoord 减去它
        // 就是那一页的探针空间原点——不用再查一次页表, 直接乘 4 换算成 texel 空间原点; pageDelta
        // 是同一个越界方向, 加到当前页的 pageIndex 上就是目标页的 pageIndex(同一张卡, pageCount/
        // directionIndex/cardCenter/cardHalfExtents/instanceIndex 都不变)。走到这个分支说明
        // pageCount>1, 邻居页恒为整页(多页卡不会子页分配)
        int2 wrappedLocal = ((probeCoordSigned % int(kProbesPerPageAxis)) + int(kProbesPerPageAxis)) % int(kProbesPerPageAxis);
        neighborPixelOrigin = (probeAtlasCoord - wrappedLocal) * 4;
        int2 pageDelta = int2(
            probeCoordSigned.x < 0 ? -1 : (probeCoordSigned.x >= int(kProbesPerPageAxis) ? 1 : 0),
            probeCoordSigned.y < 0 ? -1 : (probeCoordSigned.y >= int(kProbesPerPageAxis) ? 1 : 0));
        neighborPageIndex = uint2(int2(PC.pageIndexX, PC.pageIndexY) + pageDelta);
        neighborPageExtent = uint2(kSurfaceCachePageSize, kSurfaceCachePageSize);
    }

    int2 wrappedLocalForJitter = probeAtlasCoord - neighborPixelOrigin / 4;
    uint neighborTemporalIndex = PC.temporalIndex;
    uint2 neighborProbeTexel = uint2(wrappedLocalForJitter) * 4 + getRadiosityProbeJitter(neighborTemporalIndex);

    CardTexelInfo neighborInfo = decodeCardTexel(
        neighborProbeTexel,
        neighborPageIndex, uint2(PC.pageCountX, PC.pageCountY),
        uint2(neighborPixelOrigin), neighborPageExtent, PC.directionIndex,
        float3(PC.cardCenterX, PC.cardCenterY, PC.cardCenterZ),
        float3(PC.cardHalfExtentsX, PC.cardHalfExtentsY, PC.cardHalfExtentsZ),
        PC.instanceIndex,
        CardDepthAtlas, CardNormalAtlas);
    if (!neighborInfo.valid)
        return false;

    shR = ProbeSHRAtlas[probeAtlasCoord];
    shG = ProbeSHGAtlas[probeAtlasCoord];
    shB = ProbeSHBAtlas[probeAtlasCoord];
    probeWorldPosRel = calculateRelativeWorldPosition(neighborInfo.worldPos, referenceWorldPos);
    return true;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 groupId : SV_GroupID, uint3 localId : SV_GroupThreadID)
{
    CardPageRegion pageRegion = decodeCardPageAddress(PC.packedAddress);

    // 页的实际尺寸是 8 的倍数, 整个线程组要么全在页内要么全在页外, 且只依赖 groupId, 在 barrier 之前提前退出是一致的
    if (groupId.x * 8 >= uint(pageRegion.pixelExtent.x) || groupId.y * 8 >= uint(pageRegion.pixelExtent.y))
        return;

    // 一个线程组恰好覆盖一个 card tile, 跟 UE LumenRadiosityIntegrateCS 一样先自增再使用
    if (localId.x == 0 && localId.y == 0)
    {
        uint2 tileCoord = uint2(pageRegion.pixelOrigin) / 8 + groupId.xy;
        float numFramesAccumulated = round(RadiosityNumFramesAccumulatedAtlas[tileCoord] * 255.0);
        numFramesAccumulated = min(numFramesAccumulated + 1.0, float(kRadiosityMaxFramesAccumulated));
        RadiosityNumFramesAccumulatedAtlas[tileCoord] = numFramesAccumulated / 255.0;
        gNumFramesAccumulated = numFramesAccumulated;
    }
    GroupMemoryBarrierWithGroupSync();

    uint2 pixelInPage = groupId.xy * 8 + localId.xy;
    int2 atlasTexelCoord = pageRegion.pixelOrigin + int2(pixelInPage);

    CardTexelInfo texelInfo = decodeCardTexel(
        pixelInPage,
        uint2(PC.pageIndexX, PC.pageIndexY), uint2(PC.pageCountX, PC.pageCountY),
        uint2(pageRegion.pixelOrigin), uint2(pageRegion.pixelExtent), PC.directionIndex,
        float3(PC.cardCenterX, PC.cardCenterY, PC.cardCenterZ),
        float3(PC.cardHalfExtentsX, PC.cardHalfExtentsY, PC.cardHalfExtentsZ),
        PC.instanceIndex,
        CardDepthAtlas, CardNormalAtlas);

    if (!texelInfo.valid)
    {
        IndirectLightingAtlas[atlasTexelCoord] = float4(0, 0, 0, 1);
        return;
    }

    uint temporalIndex = PC.temporalIndex;
    uint2 probeJitter = getRadiosityProbeJitter(temporalIndex);

    int2 probeFullResCoord = max(int2(pixelInPage) - int2(probeJitter), int2(0, 0));
    int2 probeCoord00 = probeFullResCoord / 4;

    // BilinearExpand = 1: 保证四个探针里没有一个的双线性权重恰好是 0, 避免某个探针因为
    // 别的原因(比如平面加权、越到卡的真实边缘)被跳过时权重全部集中到剩下几个探针上失衡
    const float kBilinearExpand = 1.0;
    float2 bilinearWeights = (float2(probeFullResCoord - probeCoord00 * 4) + kBilinearExpand) / (4.0 + 2.0 * kBilinearExpand);
    float4 weights = float4(
        (1.0 - bilinearWeights.x) * (1.0 - bilinearWeights.y),
        bilinearWeights.x * (1.0 - bilinearWeights.y),
        (1.0 - bilinearWeights.x) * bilinearWeights.y,
        bilinearWeights.x * bilinearWeights.y);

    float4 shRSum = float4(0, 0, 0, 0);
    float4 shGSum = float4(0, 0, 0, 0);
    float4 shBSum = float4(0, 0, 0, 0);
    float weightSum = 0.0;

    // texel 自己就是这一帧算平面加权用的参考原点, 相对自己的位置恒为 0——邻居探针的世界
    // 坐标都通过 sampleProbeSH() 相对它取差
    const float3 texelWorldPosRel = float3(0, 0, 0);

    int2 probeCoords[4] = {
        probeCoord00,
        probeCoord00 + int2(1, 0),
        probeCoord00 + int2(0, 1),
        probeCoord00 + int2(1, 1),
    };
    for (int i = 0; i < 4; ++i)
    {
        float4 shR, shG, shB;
        float3 probeWorldPosRel;
        if (sampleProbeSH(probeCoords[i], texelInfo.worldPos, pageRegion.pixelOrigin, pageRegion.pixelExtent, shR, shG, shB, probeWorldPosRel))
        {
            float w = min(weights[i], calculatePlaneWeight(texelWorldPosRel, texelInfo.normalWS, probeWorldPosRel) ? 1.0 : 0.0);
            if (w > 0.0)
            {
                shRSum += shR * w;
                shGSum += shG * w;
                shBSum += shB * w;
                weightSum += w;
            }
        }
    }

    // 2×2 个探针全部无效(texel 贴着剪影边缘, 探针落在背景上)时, 向外再找一圈同一表面上的有效探针
    if (weightSum <= 0.0)
    {
        for (int dy = -1; dy <= 2; ++dy)
        {
            for (int dx = -1; dx <= 2; ++dx)
            {
                if (dx >= 0 && dx <= 1 && dy >= 0 && dy <= 1)
                    continue;

                int2 probeCoord = probeCoord00 + int2(dx, dy);
                float4 shR, shG, shB;
                float3 probeWorldPosRel;
                if (sampleProbeSH(probeCoord, texelInfo.worldPos, pageRegion.pixelOrigin, pageRegion.pixelExtent, shR, shG, shB, probeWorldPosRel)
                    && calculatePlaneWeight(texelWorldPosRel, texelInfo.normalWS, probeWorldPosRel))
                {
                    float2 offsetInProbes = (float2(pixelInPage) - float2(probeCoord * 4 + int2(probeJitter))) / 4.0;
                    float w = 1.0 / (1.0 + dot(offsetInProbes, offsetInProbes));
                    shRSum += shR * w;
                    shGSum += shG * w;
                    shBSum += shB * w;
                    weightSum += w;
                }
            }
        }
    }

    float3 irradiance = float3(0, 0, 0);
    if (weightSum > 0.0)
    {
        float4 diffuseTransferSH = calcDiffuseTransferSH(texelInfo.normalWS);
        irradiance = max(float3(0, 0, 0), dotSH(shRSum, shGSum, shBSum, diffuseTransferSH)) / weightSum;
    }

    float3 historyIrradiance = IndirectLightingAtlas[atlasTexelCoord].rgb;
    float alpha = 1.0 / (1.0 + gNumFramesAccumulated);
    irradiance = lerp(historyIrradiance, irradiance, alpha);
    irradiance = quantizeForFloatRenderTarget(irradiance, int3(atlasTexelCoord, int(temporalIndex + 2u)));

    IndirectLightingAtlas[atlasTexelCoord] = float4(irradiance, 1.0);
}
