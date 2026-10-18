// 把 SurfaceCacheAtlas 图集 texel 坐标反解成世界坐标+法线——公式是 CardCapture.slang
// VertexMain 正向投影的逆运算, 也是 MeshShapeCard.slang::sampleMeshShapeSurfaceCacheColor()
// 反查 atlas UV 那套映射的逆(那边是"已知命中点求 atlas UV", 这里是反过来"已知 atlas texel
// 求命中点", 同一个映射关系)。需要 sInstanceData, 单独拆成这个
// 文件而不是塞进 MeshShapeCard.slang——后者也被不需要 sInstanceData 的 pass(比如
// CombineFinalLighting.slang)包含
#pragma once
#include "Binding.slang"
#include "MeshShapeCard.slang"

struct CardTexelInfo
{
    bool valid;      // false 表示这个 texel 从没被 CardCapturePass 捕获过材质内容, 跳过
    WorldCoord worldPos;
    float3 normalWS;
};

// pixelInPage 是 texel 在这一页内的整数坐标([0, pageExtent)); pageExtent 是这一页实际
// 占用的宽高(texel)——子页分配的卡这里小于 kSurfaceCachePageSize, 跨页的卡(pageCount>1)
// 恒为 kSurfaceCachePageSize, 见 MeshShapeCard.slang::decodeCardPageAddress()。其余参数是这一页
// 所属 card/实例的静态描述, 调用方从各自的 push constant 里取出来传入(不同 pass 的 push
// constant 字段名不一定一样, 这里不假设有一个全局 PC)
CardTexelInfo decodeCardTexel(
    uint2 pixelInPage,
    uint2 pageIndex,
    uint2 pageCount,
    uint2 pixelOrigin,
    uint2 pageExtent,
    uint directionIndex,
    float3 cardCenter,
    float3 cardHalfExtents,
    uint instanceIndex,
    Texture2D<float> cardDepthAtlas,
    Texture2D<float4> cardNormalAtlas)
{
    CardTexelInfo info;
    info.valid = false;
    info.worldPos = (WorldCoord)0;
    info.normalWS = float3(0, 1, 0);

    // 这一段是 cardPageTexelCoord() 的逆运算, 两处的边界余量和 V 轴翻转必须一起改
    float2 minBorder, maxBorder;
    cardPageTexelBorders(pageIndex, pageCount, minBorder, maxBorder);
    float2 pageT = (float2(pixelInPage) + 0.5 - minBorder) / (float2(pageExtent) - minBorder - maxBorder);
    float2 fracPageCoord = float2(pageT.x, 1.0 - pageT.y);
    float2 pageCoordF = float2(pageIndex) + fracPageCoord;
    float2 cardUV01Raw = pageCoordF / float2(pageCount);
    float2 cardNdc = cardUV01Raw * 2.0 - 1.0;

    float3 axisX, axisY, axisZ;
    getMeshCardDirectionAxes(directionIndex, axisX, axisY, axisZ);

    float halfWidth = abs(dot(axisX, cardHalfExtents));
    float halfHeight = abs(dot(axisY, cardHalfExtents));
    float depthExtent = max(abs(dot(axisZ, cardHalfExtents)), 1e-5);

    int2 atlasTexelCoord = int2(pixelOrigin) + int2(pixelInPage);
    float sampledDepth = cardDepthAtlas[atlasTexelCoord];
    // 跟 MeshShapeCard.slang 的背景剔除同一个理由: 等于清除值说明这一页这个位置从没画过
    // 材质内容(card 聚类不保证 100% 覆盖整个表面)
    if (sampledDepth <= kCardBackgroundDepth)
        return info;

    float cardViewPosZ = (sampledDepth - 0.5) * 2.0 * depthExtent;
    float3 cardViewPos = float3(cardNdc.x * halfWidth, cardNdc.y * halfHeight, cardViewPosZ);

    float3 positionOS = cardCenter + cardViewPos.x * axisX + cardViewPos.y * axisY + cardViewPos.z * axisZ;

    InstanceData instanceData = sInstanceData[instanceIndex];
    // worldPos 的 tile 直接用这个实例自己的 tile(已经是精确对齐好的), 只在它的 offset 上
    // 加一段有界的局部偏移(mesh 自身尺度, 不会大)——不管这个实例离场景里任何"参考原点"
    // 多远都是精度安全的, 不需要经过 referenceOrigin 中转
    WorldCoord instanceTranslation = instanceData.objectToWorld.getTranslation();
    info.worldPos.tile = instanceTranslation.tile;
    info.worldPos.offset = instanceTranslation.offset + mul(positionOS, instanceData.objectToWorldRS);

    float3 normalOS = cardNormalAtlas[atlasTexelCoord].xyz * 2.0 - 1.0;
    info.normalWS = normalize(mul(normalOS, instanceData.objectToWorldNormalRot));
    info.valid = true;
    return info;
}
