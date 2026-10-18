#pragma once
#include "Math.slang"
#include "MeshCardDirection.slang"
#include "SparseDistanceField.slang"

// 跟 Source/Runtime/Renderer/SurfaceCacheAtlas.h 里的 kSurfaceCachePageSize 是同一份约定
static const int kSurfaceCachePageSize = 128;

// 跟 SurfaceCacheAtlas.h 里的 kSurfaceCacheAtlasResolution 是同一份约定
static const int kSurfaceCacheAtlasResolution = 2048;
static const int kSurfaceCacheAtlasPagesPerAxis = kSurfaceCacheAtlasResolution / kSurfaceCachePageSize;

// 捕获图集的深度清除值, 等于它说明这个 texel 没被 CardCapturePass 画过——
// 捕获端把有效深度钳在 kCardMinValidDepth 以上, 这个码值不会被真实几何占用, 照抄 UE
// EncodeSurfaceCacheDepth() 保留一个码值的做法
static const float kCardBackgroundDepth = 0.0;

// D16_UNORM 的一个最小刻度
static const float kCardMinValidDepth = 1.0 / 65535.0;

// 一次 card 采样覆盖的 4 个图集 texel 及各自权重, 权重里已经乘进了逐 texel 的深度可见性——
// 跟 UE SampleLumenCard() 的 TexelBilinearWeights * TexelVisibility 是同一个量
struct CardTexelWeights
{
    int2 texelCoord[4];
    float4 weights;
    // 双线性采样点本身在图集里的浮点 texel 坐标, 不是 texelCoord[] 那四个整数点中的任何一个
    float2 atlasTexelCoord;
};

// 打包格式与 hyp::decodeCardPageAddress()(SurfaceCacheAtlas.cpp)一致: 页号 [0,16) | 宽度档位 [16,19) | 高度档位 [19,22) | 子槽下标 [22,30)
static const uint kSurfaceCacheAtlasPackedPageIndexBits = 16u;
static const uint kSurfaceCacheAtlasPackedSizeClassBits = 3u;
static const uint kSurfaceCacheFullPageSizeClass = 4u;

// 只取出页网格下标那一段——resolveCrossPageAtlasCoord() 只处理跨页 card(恒为整页分配,
// 不会进子页 bin), 不需要尺寸档位/子槽信息, 单独暴露这个轻量版本
uint decodeSurfaceCacheAtlasPageIndex(uint packedAddress)
{
    return packedAddress & ((1u << kSurfaceCacheAtlasPackedPageIndexBits) - 1u);
}

struct CardPageRegion
{
    int2 pixelOrigin;
    int2 pixelExtent;
};

// 从 mCardPageTable 存的打包值解出这一格在图集里的像素原点+实际尺寸
CardPageRegion decodeCardPageAddress(uint packedAddress)
{
    uint pageIndex = decodeSurfaceCacheAtlasPageIndex(packedAddress);
    uint widthClass = (packedAddress >> kSurfaceCacheAtlasPackedPageIndexBits) & 0x7u;
    uint heightClass = (packedAddress >> (kSurfaceCacheAtlasPackedPageIndexBits + kSurfaceCacheAtlasPackedSizeClassBits)) & 0x7u;

    int2 gridCoord = int2(int(pageIndex) % kSurfaceCacheAtlasPagesPerAxis, int(pageIndex) / kSurfaceCacheAtlasPagesPerAxis);
    int2 pageOrigin = gridCoord * kSurfaceCachePageSize;

    CardPageRegion info;
    if (widthClass == kSurfaceCacheFullPageSizeClass && heightClass == kSurfaceCacheFullPageSizeClass)
    {
        info.pixelOrigin = pageOrigin;
        info.pixelExtent = int2(kSurfaceCachePageSize, kSurfaceCachePageSize);
        return info;
    }

    uint subSlotIndex = packedAddress >> (kSurfaceCacheAtlasPackedPageIndexBits + 2u * kSurfaceCacheAtlasPackedSizeClassBits);
    int width = 8 << int(widthClass);
    int height = 8 << int(heightClass);
    int subGridX = kSurfaceCachePageSize / width;
    int2 subCoord = int2(int(subSlotIndex) % subGridX, int(subSlotIndex) / subGridX);

    info.pixelOrigin = pageOrigin + subCoord * int2(width, height);
    info.pixelExtent = int2(width, height);
    return info;
}

// 页与页之间的内边界两侧各缩半个 texel, 让相邻两页在共享边上落到同一个卡面位置——对齐 UE
// ComputeSurfaceCacheSample 的 MinUVBorder/MaxUVBorder; card 的真实外边界不缩
void cardPageTexelBorders(uint2 pageIndex, uint2 pageCount, out float2 outMinBorder, out float2 outMaxBorder)
{
    float2 lowBorder = float2(pageIndex.x == 0u ? 0.0 : 0.5, pageIndex.y == 0u ? 0.0 : 0.5);
    float2 highBorder = float2(
        pageIndex.x + 1u == pageCount.x ? 0.0 : 0.5,
        pageIndex.y + 1u == pageCount.y ? 0.0 : 0.5);

    // V 轴翻转让低 card UV 那一侧落在大 texel 坐标一侧, 两侧余量跟着对调
    outMinBorder = float2(lowBorder.x, highBorder.y);
    outMaxBorder = float2(highBorder.x, lowBorder.y);
}

// 跨页寻址: 给定当前页内(可能越界)的局部坐标, 尝试映射到"这张卡自己的另一页"的图集绝对
// 坐标。pageSizeInUnits 是调用方的坐标单位下一页的边长——TraceRadiance/[B] 是 texel 空间传
// kSurfaceCachePageSize(128), ProbeSH/[D] 是探针空间传 kSurfaceCachePageSize/4(32), 两者共用同一
// 套页网格(页号对 kSurfaceCacheAtlasPagesPerAxis 取模得到的页网格是页级别的, 跟内部按 texel 还是
// 探针计量无关)。只有 pageCount.x>1 || pageCount.y>1 的多页 card 才可能命中——单页 card 的
// 页边界就是表面几何边缘, 会落进 return false 分支, 这是正确行为不是简化, 见
// Document/Lumen_Radiosity_Plan.md §3 [B]/[D]
bool resolveCrossPageAtlasCoord(
    int2 localCoord,
    int pageSizeInUnits,
    uint2 pageIndex,
    uint2 pageCount,
    uint pageIndirectionOffset,
    StructuredBuffer<uint> cardPages,
    out int2 atlasCoord)
{
    int2 pageDelta = int2(0, 0);
    int2 wrappedLocal = localCoord;
    for (int axis = 0; axis < 2; ++axis)
    {
        if (localCoord[axis] < 0)
        {
            pageDelta[axis] = -1;
            wrappedLocal[axis] += pageSizeInUnits;
        }
        else if (localCoord[axis] >= pageSizeInUnits)
        {
            pageDelta[axis] = 1;
            wrappedLocal[axis] -= pageSizeInUnits;
        }
    }

    int2 neighborPageIndex = int2(pageIndex) + pageDelta;
    if (any(neighborPageIndex < int2(0, 0)) || any(neighborPageIndex >= int2(pageCount)))
    {
        atlasCoord = int2(0, 0);
        return false;
    }

    uint flatIndex = uint(neighborPageIndex.y) * pageCount.x + uint(neighborPageIndex.x);
    uint packedAddress = cardPages[pageIndirectionOffset + flatIndex];
    // 跨页的卡恒为整页分配(不会进子页 bin, 见 SurfaceCachePageAllocator::allocate()),
    // 只需要页网格下标, 不需要尺寸档位/子槽信息
    uint pageIndex = decodeSurfaceCacheAtlasPageIndex(packedAddress);
    int2 gridCoord = int2(int(pageIndex) % kSurfaceCacheAtlasPagesPerAxis, int(pageIndex) / kSurfaceCacheAtlasPagesPerAxis);
    atlasCoord = gridCoord * pageSizeInUnits + wrappedLocal;
    return true;
}

// pageExtent: 这一页在图集里实际占用的宽高(texel)——子页分配的卡这里小于
// kSurfaceCachePageSize, 跨页的卡(pageCount>1)恒为 kSurfaceCachePageSize, 见 decodeCardPageAddress()
float2 cardPageTexelCoord(float2 pageCoordF, uint2 pageIndex, uint2 pageCount, float2 pageExtent)
{
    float2 minBorder, maxBorder;
    cardPageTexelBorders(pageIndex, pageCount, minBorder, maxBorder);

    // 捕获时视口是负高度翻转的(Vulkan/CommandList.cpp::setViewportBackend), NDC y = +1 落在
    // 页面顶部, 反查必须用同一个映射
    float2 t = frac(pageCoordF);
    t.y = 1.0 - t.y;

    // 跟 UE 的 SubTexelPrecisionBias 同一个用途: 保证 floor() 之后 +1 的那个 texel 仍落在本页内
    const float kSubTexelPrecisionBias = 1.0 / 512.0;
    float2 texelCoordF = t * (pageExtent - minBorder - maxBorder) + minBorder;
    return clamp(texelCoordF, float2(0.5, 0.5), pageExtent - float2(0.5 + kSubTexelPrecisionBias, 0.5 + kSubTexelPrecisionBias));
}

// 深度这里必须用整数下标逐 texel 读, 不能交给 sampler 插值: CardDepthAtlas 底层是真正的深度
// 格式(D16_UNORM), WGSL 里对应 texture_depth_2d, 规范上不支持用普通 sampler 做双线性插值
// pixelOrigin/pageExtent: 这一页在图集里的绝对像素原点+实际占用宽高, 见 decodeCardPageAddress()
CardTexelWeights computeCardTexelWeights(
    Texture2D<float> cardDepthAtlas,
    int2 pixelOrigin,
    int2 pageExtent,
    float2 texelCoordF,
    float expectedDepth,
    float depthBiasThreshold,
    float depthBiasFalloff)
{
    float2 baseF = floor(texelCoordF - 0.5);
    float2 fracUV = texelCoordF - 0.5 - baseF;
    int2 base = clamp(int2(baseF), int2(0, 0), pageExtent - int2(2, 2));
    int2 atlasOrigin = pixelOrigin;

    float4 bilinearWeights = float4(
        (1.0 - fracUV.x) * (1.0 - fracUV.y),
        fracUV.x * (1.0 - fracUV.y),
        (1.0 - fracUV.x) * fracUV.y,
        fracUV.x * fracUV.y);

    CardTexelWeights result;
    result.atlasTexelCoord = float2(atlasOrigin) + texelCoordF;
    result.texelCoord[0] = atlasOrigin + base + int2(0, 0);
    result.texelCoord[1] = atlasOrigin + base + int2(1, 0);
    result.texelCoord[2] = atlasOrigin + base + int2(0, 1);
    result.texelCoord[3] = atlasOrigin + base + int2(1, 1);

    for (int i = 0; i < 4; ++i)
    {
        float sampledDepth = cardDepthAtlas[result.texelCoord[i]];
        float visibility = sampledDepth <= kCardBackgroundDepth
            ? 0.0
            : 1.0 - saturate((abs(sampledDepth - expectedDepth) - depthBiasThreshold) / depthBiasFalloff);
        result.weights[i] = bilinearWeights[i] * visibility;
    }
    return result;
}

float3 sampleCardAtlasTexels(Texture2D<float4> atlas, CardTexelWeights texelWeights, float4 normalizedWeights)
{
    float3 sum = float3(0.0, 0.0, 0.0);
    for (int i = 0; i < 4; ++i)
        sum += atlas[texelWeights.texelCoord[i]].rgb * normalizedWeights[i];
    return sum;
}

// 给定 SDF 命中点(局部空间)+ 法线, 反查该用哪几张 card、按什么权重混合它们的 albedo——
// 照抄 UE Lumen 的 SampleLumenMeshCards 思路(不是空间索引查表, 是"追踪本来就知道是哪个
// mesh + 按方向分桶位图 + 命中点做 AABB 测试筛出候选卡 + 按轴权重和深度可见性加权混合"):
//   1. 法线各分量平方当轴权重, 非零的轴(最多 3 个)按符号从 CardLookup 取出对应方向的位图,
//      合并成候选卡集合——不需要遍历这个 mesh 的全部卡片。
//   2. 对候选卡做局部空间 AABB 测试, 筛掉不覆盖这个点的。
//   3. 通过测试的卡各自算 UV、查页间接索引换算出图集像素坐标采样, 按"轴权重 × 深度可见性
//      权重"加权累加, 不是选一张赢家——这样在棱角处(法线同时有两个轴分量)才有平滑过渡,
//      不会两个方向的卡硬切换出接缝。
//   4. 一张卡的候选权重都太小(比如命中点根本不在任何卡的覆盖范围里, card 聚类本来就不
//      保证 100% 覆盖整个表面)就通过 outHit=false 告诉调用方这次没采到, 由调用方决定
//      怎么标注(可视化 pass 用醒目色标出来, 不留黑洞也不能悄悄污染真实颜色)。
//
// meshShapeDataOffset 是这个物体在 hyp::MeshShapeDataTable 里的区间起始下标, resolution
// 是它的 SDF 稠密体素分辨率(用来现算 CardLookup 在共享表里的偏移, 见
// meshShapeCardLookupOffset()), cardRangeSlotBase 是这个实例的 card ranges 记录在
// CardPages 共享表里的起始下标——原来这段逻辑是 MeshSDFScene.slang 里绑死单个物体的私有
// 函数, Global Distance Field 命中查询也需要对任意解析出来的候选物体做同样的事, 提出来
// 给两边共用。
float3 sampleMeshShapeSurfaceCacheColor(
    StructuredBuffer<uint> meshShapeData,
    StructuredBuffer<uint> cardPages,
    Texture2D<float4> cardAlbedoAtlas,
    Texture2D<float> cardDepthAtlas,
    SamplerState linearSampler,
    uint meshShapeDataOffset,
    int3 resolution,
    int cardCount,
    uint cardRangeSlotBase,
    float3 positionOS,
    float3 normalOS,
    float surfaceCacheBias, // 命中点预期的世界空间位置误差下限, 来自粗糙 cascade 追踪的调用方
                            // 传一个跟体素大小挂钩的值, 精细 per-mesh 追踪传一个固定常数——
                            // 跟 UE SampleLumenCard() 的 SurfaceCacheBias 同一个用途, UE 的
                            // per-mesh 追踪路径也是固定传 20 个世界单位(厘米), 不是 0
    float3 instanceWorldScale, // 实例三根物体轴各自的世界缩放, 把 surfaceCacheBias 换算进物体空间用
    out bool outHit)
{
    uint cardLookupOffset = meshShapeCardLookupOffset(meshShapeDataOffset, resolution);
    uint cardDataOffset = cardLookupOffset + kMeshShapeCardLookupWordCount;

    float3 axisWeight = normalOS * normalOS;

    uint candidateMask = 0;
    if (axisWeight.x > 0.0) candidateMask |= meshShapeData[cardLookupOffset + (normalOS.x < 0.0 ? 1 : 0)];
    if (axisWeight.y > 0.0) candidateMask |= meshShapeData[cardLookupOffset + (normalOS.y < 0.0 ? 3 : 2)];
    if (axisWeight.z > 0.0) candidateMask |= meshShapeData[cardLookupOffset + (normalOS.z < 0.0 ? 5 : 4)];

    float3 colorSum = float3(0.0, 0.0, 0.0);
    float weightSum = 0.0;

    for (uint cardIndex = 0; cardIndex < uint(cardCount); ++cardIndex)
    {
        if ((candidateMask & (1u << cardIndex)) == 0)
            continue;

        // CardData: 7 个 uint32 一张卡, center.xyz/halfExtents.xyz 用 asfloat() 重新
        // 解释成 float, 最后一个是原始的 uint directionIndex——见 hyp::MeshCard
        uint cardBase = cardDataOffset + cardIndex * 7u;
        float3 cardCenter = float3(asfloat(meshShapeData[cardBase + 0]), asfloat(meshShapeData[cardBase + 1]), asfloat(meshShapeData[cardBase + 2]));
        float3 cardHalfExtents = float3(asfloat(meshShapeData[cardBase + 3]), asfloat(meshShapeData[cardBase + 4]), asfloat(meshShapeData[cardBase + 5]));
        uint directionIndex = meshShapeData[cardBase + 6];

        float3 axisX, axisY, axisZ;
        getMeshCardDirectionAxes(directionIndex, axisX, axisY, axisZ);

        float3 rel = positionOS - cardCenter;
        float3 cardViewPos = float3(dot(rel, axisX), dot(rel, axisY), dot(rel, axisZ));

        // 跟 CardCapture.slang 同一套约定: halfWidth/halfHeight/depthExtent 必须取 abs(),
        // 跟 axisX/axisY/axisZ 具体朝哪边无关, 只表示"宽度"
        float halfWidth = abs(dot(axisX, cardHalfExtents));
        float halfHeight = abs(dot(axisY, cardHalfExtents));
        float depthExtent = max(abs(dot(axisZ, cardHalfExtents)), 1e-5);

        // 三根卡轴都是 ±物体轴, 所以世界容差逐轴除以那根轴的世界缩放就是精确的物体空间容差
        float3 cardAxisWorldScale = float3(
            abs(dot(axisX, instanceWorldScale)),
            abs(dot(axisY, instanceWorldScale)),
            abs(dot(axisZ, instanceWorldScale)));
        float3 localBias = surfaceCacheBias / max(cardAxisWorldScale, float3(1e-6));

        // 跟 UE LumenSurfaceCacheSampling.ush::SampleLumenCard() 一致: xy 和 z 一样放宽
        // 0.5*surfaceCacheBias 再判定, 棱上因为法线/命中点插值误差落在卡边界外一点点的采样
        // 不会被直接拒绝——通过判定之后把 cardViewPos.xy clamp 回卡面矩形内再算 UV, 相当于
        // 吸附到最近的边缘 texel, 不是真的把范围以外的内容画进来
        if (any(abs(cardViewPos.xy) > float2(halfWidth, halfHeight) + 0.5 * localBias.xy) || abs(cardViewPos.z) > depthExtent + 0.5 * localBias.z)
            continue;
        cardViewPos.xy = clamp(cardViewPos.xy, -float2(halfWidth, halfHeight), float2(halfWidth, halfHeight));

        // 这个实例的 ranges 记录从 cardRangeSlotBase 开始, 3 个 uint32 一张卡:
        // pageIndirectionOffset/pageCount.x/pageCount.y
        uint rangeBase = cardRangeSlotBase + cardIndex * 3u;
        uint pageIndirectionOffset = cardPages[rangeBase + 0];
        uint pageCountX = cardPages[rangeBase + 1];
        uint pageCountY = cardPages[rangeBase + 2];
        if (pageCountX == 0 || pageCountY == 0)
            continue; // 这张卡在 SurfaceCacheAtlas 里分配失败(池子被占满), 没有内容可采样

        float2 cardNdc = float2(cardViewPos.x / halfWidth, cardViewPos.y / halfHeight); // [-1,1]

        float2 cardUV01Raw = clamp(cardNdc * 0.5 + 0.5, 0.0, 0.999999);

        float2 pageCoordF = cardUV01Raw * float2(pageCountX, pageCountY);
        uint2 pageIndex = min(uint2(pageCoordF), uint2(pageCountX, pageCountY) - uint2(1, 1));
        uint flatPageIndex = pageIndirectionOffset + pageIndex.y * pageCountX + pageIndex.x;
        // pageIndirectionOffset 本身就是 payload 在 CardPages 共享空间里的绝对下标, 不需要
        // 再加任何固定偏移
        uint packedAddress = cardPages[flatPageIndex];

        // 跟 hyp::decodeCardPageAddress()(SurfaceCacheAtlas.cpp)同一套打包格式, 两边必须保持一致
        CardPageRegion pageRegion = decodeCardPageAddress(packedAddress);
        float2 texelCoordF = cardPageTexelCoord(pageCoordF, pageIndex, uint2(pageCountX, pageCountY), float2(pageRegion.pixelExtent));

        // 深度可见性权重: depth atlas 里存的是材质捕获那一刻的 ndcZ(近=1 远=0, 跟
        // CardCapture.slang 同一个约定), 拿命中点自己在这张卡视图空间里的深度按同一套
        // 公式换算、跟 atlas 里记录的值比——差太多说明这个点实际上被遮挡/不属于这张卡
        // 朝向的那层表面(比如背面或者内部凹陷处), 压低权重, 不能直接信这次采样
        float expectedDepth = 0.5 + cardViewPos.z / (2.0 * depthExtent);
        // 跟 UE SampleLumenCard() 的 BiasTreshold = SurfaceCacheBias / LocalExtent.z、
        // BiasFalloff = 0.25f * BiasTreshold 换算方式一致——falloff 比阈值本身还要陡,
        // 深度差一超过阈值就快速掉到 0 权重, 不让明显对不上深度的候选卡还占着可观权重
        float depthBiasThreshold = localBias.z / depthExtent;
        float depthBiasFalloff = 0.25 * depthBiasThreshold;

        CardTexelWeights texelWeights = computeCardTexelWeights(
            cardDepthAtlas, pageRegion.pixelOrigin, pageRegion.pixelExtent, texelCoordF, expectedDepth, depthBiasThreshold, depthBiasFalloff);
        float texelWeightSum = dot(texelWeights.weights, float4(1.0));
        if (texelWeightSum <= 0.0)
            continue;

        float axisW = (directionIndex == 0u || directionIndex == 1u) ? axisWeight.x
            : (directionIndex == 2u || directionIndex == 3u) ? axisWeight.y
            : axisWeight.z;

        // 卡权重带上 texelWeightSum: 4 个 texel 里有几个是背景或者深度对不上, 这张卡的
        // 话语权就按比例降下来, 而不是整张卡一刀切掉
        float weight = axisW * texelWeightSum;
        if (weight <= 0.0)
            continue;

        float3 albedo = sampleCardAtlasTexels(cardAlbedoAtlas, texelWeights, texelWeights.weights / texelWeightSum);
        colorSum += albedo * weight;
        weightSum += weight;
    }

    if (weightSum < 1e-4)
    {
        outHit = false;
        return float3(1.0, 1.0, 0.0); // 黄色: 有 card 数据但没有任何一张覆盖到这个点
    }

    outHit = true;
    return colorSum / weightSum;
}

// 跟 UE VISUALIZE_MODE_CARD_WEIGHTS 完全一致(LumenSurfaceCacheSampling.ush): 对全部
// card index 统一用这套取模公式着色, 不对任何 index 特殊处理
float3 cardDebugIndexColor(uint index)
{
    float3 color;
    color.x = float(index % 4u) / 3.0;
    color.y = float((index / 4u) % 4u) / 3.0;
    color.z = saturate(1.0 - color.x - color.y);
    return color;
}

// 跟 UE VISUALIZE_MODE_CARD_WEIGHTS 的格线间距一致
static const float kSurfaceCacheAtlasDebugGridPeriodInTexels = 8.0;

float surfaceCacheAtlasDebugGridShade(float2 atlasTexelCoord)
{
    float2 coordInCell = frac(atlasTexelCoord / kSurfaceCacheAtlasDebugGridPeriodInTexels) * kSurfaceCacheAtlasDebugGridPeriodInTexels;
    return any(coordInCell < 1.0) ? 0.25 : 1.0;
}

// 各项都是还没除以 weightSum 的加权和, 归一化交给调用方
struct MeshShapeCardSampleAccumulator
{
    float3 albedoSum;
    float3 normalSum;   // 物体局部空间
    float3 lightingSum; // FinalLightingAtlas 采样值(已经是 albedo/PI * direct + emissive 的结果)
    float3 emissiveSum;
    float3 directLightingSum;   // DirectLightingAtlas 原始采样, 不乘 albedo
    float3 indirectLightingSum; // IndirectLightingAtlas 原始采样, 不乘 albedo
    float weightSum;
    // 通过 AABB 和页有效性筛选的卡的轴权重之和, 不含逐 texel 可见性——weightSum 除以它就是
    // 纯可见性项, 两者分开才能判断不连续是卡的进出造成的还是 texel 判定造成的
    float axisWeightSum;
    // 逐卡取色后按权重混合的结果, 只有可视化用得上——必须在循环里逐卡算完再加权, 取
    // 权重最大的那一张会把本该渐变的过渡画成硬色块
    float3 debugColorSum;
};

// 跟 sampleMeshShapeSurfaceCacheColor() 是同一套候选卡筛选和权重计算
MeshShapeCardSampleAccumulator sampleMeshShapeSurfaceCache(
    StructuredBuffer<uint> meshShapeData,
    StructuredBuffer<uint> cardPages,
    Texture2D<float4> cardFinalLightingAtlas,
    Texture2D<float4> cardAlbedoAtlas,
    Texture2D<float4> cardNormalAtlas,
    Texture2D<float4> cardEmissiveAtlas,
    Texture2D<float4> cardDirectLightingAtlas,
    Texture2D<float4> cardIndirectLightingAtlas,
    Texture2D<float> cardDepthAtlas,
    uint meshShapeDataOffset,
    int3 resolution,
    int cardCount,
    uint cardRangeSlotBase,
    float3 positionOS,
    float3 normalOS,
    float surfaceCacheBias,
    float3 instanceWorldScale)
{
    MeshShapeCardSampleAccumulator acc;
    acc.albedoSum = float3(0.0);
    acc.normalSum = float3(0.0);
    acc.lightingSum = float3(0.0);
    acc.emissiveSum = float3(0.0);
    acc.directLightingSum = float3(0.0);
    acc.indirectLightingSum = float3(0.0);
    acc.weightSum = 0.0;
    acc.axisWeightSum = 0.0;
    acc.debugColorSum = float3(0.0);

    uint cardLookupOffset = meshShapeCardLookupOffset(meshShapeDataOffset, resolution);
    uint cardDataOffset = cardLookupOffset + kMeshShapeCardLookupWordCount;

    float3 axisWeight = normalOS * normalOS;

    uint candidateMask = 0;
    if (axisWeight.x > 0.0) candidateMask |= meshShapeData[cardLookupOffset + (normalOS.x < 0.0 ? 1 : 0)];
    if (axisWeight.y > 0.0) candidateMask |= meshShapeData[cardLookupOffset + (normalOS.y < 0.0 ? 3 : 2)];
    if (axisWeight.z > 0.0) candidateMask |= meshShapeData[cardLookupOffset + (normalOS.z < 0.0 ? 5 : 4)];

    for (uint cardIndex = 0; cardIndex < uint(cardCount); ++cardIndex)
    {
        if ((candidateMask & (1u << cardIndex)) == 0)
            continue;

        uint cardBase = cardDataOffset + cardIndex * 7u;
        float3 cardCenter = float3(asfloat(meshShapeData[cardBase + 0]), asfloat(meshShapeData[cardBase + 1]), asfloat(meshShapeData[cardBase + 2]));
        float3 cardHalfExtents = float3(asfloat(meshShapeData[cardBase + 3]), asfloat(meshShapeData[cardBase + 4]), asfloat(meshShapeData[cardBase + 5]));
        uint directionIndex = meshShapeData[cardBase + 6];

        float3 axisX, axisY, axisZ;
        getMeshCardDirectionAxes(directionIndex, axisX, axisY, axisZ);

        float3 rel = positionOS - cardCenter;
        float3 cardViewPos = float3(dot(rel, axisX), dot(rel, axisY), dot(rel, axisZ));

        float halfWidth = abs(dot(axisX, cardHalfExtents));
        float halfHeight = abs(dot(axisY, cardHalfExtents));
        float depthExtent = max(abs(dot(axisZ, cardHalfExtents)), 1e-5);

        float3 cardAxisWorldScale = float3(
            abs(dot(axisX, instanceWorldScale)),
            abs(dot(axisY, instanceWorldScale)),
            abs(dot(axisZ, instanceWorldScale)));
        float3 localBias = surfaceCacheBias / max(cardAxisWorldScale, float3(1e-6));

        // 跟 sampleMeshShapeSurfaceCacheColor() 同一处修复: xy 也放宽 0.5*localBias,
        // 通过判定后 clamp 回卡面矩形, 见该函数的注释
        if (any(abs(cardViewPos.xy) > float2(halfWidth, halfHeight) + 0.5 * localBias.xy) || abs(cardViewPos.z) > depthExtent + 0.5 * localBias.z)
            continue;
        cardViewPos.xy = clamp(cardViewPos.xy, -float2(halfWidth, halfHeight), float2(halfWidth, halfHeight));

        uint rangeBase = cardRangeSlotBase + cardIndex * 3u;
        uint pageIndirectionOffset = cardPages[rangeBase + 0];
        uint pageCountX = cardPages[rangeBase + 1];
        uint pageCountY = cardPages[rangeBase + 2];
        if (pageCountX == 0 || pageCountY == 0)
            continue;

        float axisW = (directionIndex == 0u || directionIndex == 1u) ? axisWeight.x
            : (directionIndex == 2u || directionIndex == 3u) ? axisWeight.y
            : axisWeight.z;
        acc.axisWeightSum += axisW;

        float2 cardNdc = float2(cardViewPos.x / halfWidth, cardViewPos.y / halfHeight);
        float2 cardUV01Raw = clamp(cardNdc * 0.5 + 0.5, 0.0, 0.999999);

        float2 pageCoordF = cardUV01Raw * float2(pageCountX, pageCountY);
        uint2 pageIndex = min(uint2(pageCoordF), uint2(pageCountX, pageCountY) - uint2(1, 1));
        uint flatPageIndex = pageIndirectionOffset + pageIndex.y * pageCountX + pageIndex.x;
        uint packedAddress = cardPages[flatPageIndex];

        CardPageRegion pageRegion = decodeCardPageAddress(packedAddress);

        float2 texelCoordF = cardPageTexelCoord(pageCoordF, pageIndex, uint2(pageCountX, pageCountY), float2(pageRegion.pixelExtent));

        float expectedDepth = 0.5 + cardViewPos.z / (2.0 * depthExtent);
        float depthBiasThreshold = localBias.z / depthExtent;
        float depthBiasFalloff = 0.25 * depthBiasThreshold;

        CardTexelWeights texelWeights = computeCardTexelWeights(
            cardDepthAtlas, pageRegion.pixelOrigin, pageRegion.pixelExtent, texelCoordF, expectedDepth, depthBiasThreshold, depthBiasFalloff);
        float texelWeightSum = dot(texelWeights.weights, float4(1.0));
        if (texelWeightSum <= 0.0)
            continue;

        float weight = axisW * texelWeightSum;
        if (weight <= 0.0)
            continue;

        float4 normalizedTexelWeights = texelWeights.weights / texelWeightSum;
        // FinalLightingAtlas 已经是 CombineFinalLightingPass 按 albedo/π·direct+emissive 合成好的
        // 结果, 这里直接采样加权, 不需要再现场重新组合
        float3 finalLighting = sampleCardAtlasTexels(cardFinalLightingAtlas, texelWeights, normalizedTexelWeights);
        float3 albedo = sampleCardAtlasTexels(cardAlbedoAtlas, texelWeights, normalizedTexelWeights);
        // NormalAtlas 存的是局部空间法线按 *0.5+0.5 压进 UNORM8 的结果, 见 CardCapture.slang
        float3 cardNormalOS = sampleCardAtlasTexels(cardNormalAtlas, texelWeights, normalizedTexelWeights) * 2.0 - 1.0;
        float3 emissive = sampleCardAtlasTexels(cardEmissiveAtlas, texelWeights, normalizedTexelWeights);
        float3 directLighting = sampleCardAtlasTexels(cardDirectLightingAtlas, texelWeights, normalizedTexelWeights);
        float3 indirectLighting = sampleCardAtlasTexels(cardIndirectLightingAtlas, texelWeights, normalizedTexelWeights);

        acc.albedoSum += albedo * weight;
        acc.normalSum += cardNormalOS * weight;
        acc.lightingSum += finalLighting * weight;
        acc.emissiveSum += emissive * weight;
        acc.directLightingSum += directLighting * weight;
        acc.indirectLightingSum += indirectLighting * weight;
        acc.weightSum += weight;
        acc.debugColorSum += cardDebugIndexColor(cardIndex) * surfaceCacheAtlasDebugGridShade(texelWeights.atlasTexelCoord) * weight;
    }

    return acc;
}
