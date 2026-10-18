#pragma once

// 稀疏体素距离场的单物体采样逻辑——per-mesh SDF 数据(brick 分块 + 跨网格共享的间接索引表 +
// 共享 brick atlas)的通用查询函数, 被 MeshSDF/MeshSDFScene.slang(单物体可视化, 沿射线连续
// 采样)和 GDF 合成 compute shader(每个候选物体各自调用一次, 取多个物体的 min)共用, 不再
// 各自维护一份重复代码。

// 跟 Source/Runtime/RenderBase/DistanceFieldBrickAtlas.h 里的 kSparseBrickSize/
// kSparseUniqueBrickSize/kInvalidSparseBrickIndex 是同一份约定, 数值必须一致
static const int kSparseBrickSize = 8;
static const int kSparseUniqueBrickSize = 7;
static const uint kInvalidSparseBrickIndex = 0xFFFFFFFFu;

// 有符号距离值 <-> R8_UNORM 字节的编解码, 对称线性映射 [-range,range] <-> [0,1]——跟
// Source/Runtime/Engine/Mesh/StaticMesh.cpp::encodeSignedDistance() 数值上完全对称,
// 改这里的公式要同步改那边。brick atlas 纹理本身声明成 Texture3D<float>/RWTexture3D<float>
// (采样/读写返回的浮点数本来就已经是硬件自动转换好的 [0,1] unorm 值), 这两个函数只负责
// [0,1] <-> 有符号距离值这一层线性映射, 不涉及位打包
float decodeSignedDistance(float encoded, float range)
{
    return (encoded * 2.0 - 1.0) * range;
}

float encodeSignedDistance(float value, float range)
{
    float t = clamp(value / range, -1.0, 1.0);
    return t * 0.5 + 0.5;
}

// 一个物体自己的 SDF 元信息, 跟 hyp::MeshShapeProxy 持有的字段逐一对应——brickIndirection/
// brickAtlas 是资源类型(buffer/texture), HLSL/Slang 里资源句柄不能作为结构体成员出现,
// 单独当参数传给下面的函数, 不塞进这个结构体
struct SparseDistanceFieldObject
{
    float3 boundsMin;
    float3 boundsMax;
    float narrowBandDistance;
    int3 resolution;             // 稠密体素分辨率
    int atlasBrickGridExtent;    // atlas 每根轴排了多少个 brick(不是体素数); 强制立方体, 只存 1 个分量
    uint indirectionTableOffset; // 这个物体自己的间接索引表在跨网格共享表里的起始下标
};

// 整数坐标 i 落在第 i 个角点采样上, 跟 UE 的 IndirectionPos 同一约定
float3 sparseDistanceFieldPosToVoxelCoord(SparseDistanceFieldObject obj, float3 positionOS)
{
    return (positionOS - obj.boundsMin) / (obj.boundsMax - obj.boundsMin) * float3(obj.resolution);
}

// brick 槽位号 -> atlas 里的整数体素原点——跟 hyp::DistanceFieldBrickAtlas::slotToAtlasVoxelOrigin
// 同一套编码(slot 按 x 变化最快、然后 y、然后 z 拆成三维网格坐标, 乘 brick 边长), 两边必须
// 保持一致。atlasBrickGridExtent 是标量(brick atlas 强制立方体, 三个轴共用同一个值)
int3 sparseBrickSlotToAtlasOrigin(uint slot, int atlasBrickGridExtent)
{
    int3 gridCoord = int3(
        int(slot) % atlasBrickGridExtent,
        (int(slot) / atlasBrickGridExtent) % atlasBrickGridExtent,
        int(slot) / (atlasBrickGridExtent * atlasBrickGridExtent)
    );
    return gridCoord * kSparseBrickSize;
}

// 从 MeshShapeDataTable 共享表(hyp::MeshShapeDataTable)里第 offset 段读出 metadata,
// 手动按 kMeshShapeMetadataWordCount 个 uint32 一组取值(不用 reinterpret_cast 做
// 类型双关), 顺序跟 hyp::MeshShapeMetadata 逐字段对应(最后一个 cardCount 字段这里
// 不用, 是 Card 采样那条路径读的, 见 MeshSDFScene.slang::sampleSurfaceCacheColor);
// indirectionTableOffset 紧跟在同一段区间的 metadata 后面, 不用单独存, atlasBrickGridExtent
// 是 DistanceFieldBrickAtlas 级别的全局常量, 由调用方传入
static const uint kMeshShapeMetadataWordCount = 11;

// CardLookup 紧跟在间接索引表后面, 固定 6 个 uint32——跟 hyp::kMeshShapeCardLookupWordCount
// 必须保持一致
static const uint kMeshShapeCardLookupWordCount = 6;

// brickGridResolution 不额外存, 现算: ceil(resolution / kSparseUniqueBrickSize), 必须跟
// Mesh.cpp::setSDFVolumeData() 里算 brickGridResolution 的公式完全一致。resolution 全 0
// (没有 SDF 数据的网格)时这个函数自然返回 0, 间接索引表长度也随之为 0, 不需要另外判断
int3 sparseDistanceFieldBrickGridResolution(int3 resolution)
{
    return (resolution + (kSparseUniqueBrickSize - 1)) / kSparseUniqueBrickSize;
}

int3 loadMeshShapeResolution(StructuredBuffer<uint> objectTable, uint offset)
{
    return int3(objectTable[offset + 7], objectTable[offset + 8], objectTable[offset + 9]);
}

SparseDistanceFieldObject loadSparseDistanceFieldObject(StructuredBuffer<uint> objectTable, uint offset, int atlasBrickGridExtent)
{
    SparseDistanceFieldObject obj;
    obj.boundsMin = asfloat(uint3(objectTable[offset + 0], objectTable[offset + 1], objectTable[offset + 2]));
    obj.boundsMax = asfloat(uint3(objectTable[offset + 3], objectTable[offset + 4], objectTable[offset + 5]));
    obj.narrowBandDistance = asfloat(objectTable[offset + 6]);
    obj.resolution = loadMeshShapeResolution(objectTable, offset);
    obj.atlasBrickGridExtent = atlasBrickGridExtent;
    obj.indirectionTableOffset = offset + kMeshShapeMetadataWordCount;
    return obj;
}

// 这个网格的 CardLookup 在共享表里的起始下标——紧跟在间接索引表后面。objectTableOffset 是
// MeshShapeDataTable 里这个网格的区间起始下标(不是 obj.indirectionTableOffset), resolution
// 用来现算间接索引表长度
uint meshShapeCardLookupOffset(uint objectTableOffset, int3 resolution)
{
    int3 brickGridResolution = sparseDistanceFieldBrickGridResolution(resolution);
    uint indirectionCount = uint(brickGridResolution.x * brickGridResolution.y * brickGridResolution.z);
    return objectTableOffset + kMeshShapeMetadataWordCount + indirectionCount;
}

// metadata 的最后一个字, 见 hyp::MeshShapeMetadata::cardCount
uint loadMeshShapeCardCount(StructuredBuffer<uint> objectTable, uint objectTableOffset)
{
    return objectTable[objectTableOffset + kMeshShapeMetadataWordCount - 1u];
}

// 定位 voxelCoord 落在哪个 brick, 输出这个点在 atlas 里对应的体素坐标(brick 的整数对齐
// 原点 + 块内小数偏移)。返回 false 表示这一片没有 brick 数据(格子越界, 或者间接索引表里
// 是空哨兵), 调用方按"窄带外"处理。
bool sparseDistanceFieldResolveBrick(
    SparseDistanceFieldObject obj,
    StructuredBuffer<uint> brickIndirection,
    float3 voxelCoord,
    out float3 atlasVoxel)
{
    atlasVoxel = float3(0.0);

    int3 brickGridResolution = sparseDistanceFieldBrickGridResolution(obj.resolution);
    if (any(brickGridResolution <= 0))
        return false;

    float3 clampedVoxelCoord = clamp(voxelCoord, float3(0.0), float3(obj.resolution));
    // 坐标正好等于 resolution 时落在最后一个 brick 的重叠边界 texel 上, 不能算到下一个 brick
    int3 brickCoord = min(int3(floor(clampedVoxelCoord / float(kSparseUniqueBrickSize))), brickGridResolution - 1);

    int brickLinearIndex = brickCoord.x
        + brickCoord.y * brickGridResolution.x
        + brickCoord.z * brickGridResolution.x * brickGridResolution.y;
    // brickIndirection 是跨网格共享表, 这个物体自己的区间从 obj.indirectionTableOffset 开始
    uint slot = brickIndirection[obj.indirectionTableOffset + brickLinearIndex];
    if (slot == kInvalidSparseBrickIndex)
        return false;

    float3 atlasOrigin = float3(sparseBrickSlotToAtlasOrigin(slot, obj.atlasBrickGridExtent));
    float3 localOffset = clampedVoxelCoord - float3(brickCoord) * float(kSparseUniqueBrickSize);
    atlasVoxel = atlasOrigin + localOffset;
    return true;
}

// 只采样"clamp 到包围盒之后"的窄带纹理值, 不含点在包围盒外那部分的解析距离——单独拆出来
// 给 GlobalDistanceFieldComposite.slang 用: 包围盒外的距离要逐轴按各自的世界缩放换算,
// 不能跟这个纯局部空间的窄带值共用同一个折算比例, 见该文件里的调用处
float sampleSparseDistanceFieldClamped(
    SparseDistanceFieldObject obj,
    StructuredBuffer<uint> brickIndirection,
    Texture3D<float> brickAtlas,
    SamplerState linearSampler,
    float3 clampedPositionOS)
{
    float3 atlasVoxel;
    if (!sparseDistanceFieldResolveBrick(obj, brickIndirection, sparseDistanceFieldPosToVoxelCoord(obj, clampedPositionOS), atlasVoxel))
        return obj.narrowBandDistance;

    float3 atlasExtent = float3((float)(obj.atlasBrickGridExtent * kSparseBrickSize));
    float3 uvw = (atlasVoxel + 0.5) / atlasExtent;
    float encoded = brickAtlas.SampleLevel(linearSampler, uvw, 0.0);
    return decodeSignedDistance(encoded, obj.narrowBandDistance);
}

// 局部空间点 -> 距离值。查询点在去掉边界体素的包围盒之外时, 先夹回包围盒采样再加上
// 点到包围盒的解析距离, 而不是直接返回 obj.narrowBandDistance——否则包围盒外的点不管
// 实际多远都会得到同一个常数, 被 GDF 合成拿去跟其它候选物体取 min() 时把周围体素的距离
// 值错误地拉小, 照抄 UE MeshDistanceFieldCommon.ush::DistanceToNearestSurfaceForObject()
// 的做法。查不到对应 brick 但仍在包围盒内, 说明这一片是稀疏打包时跳过的空 brick, 表示
// "这一片没有表面", 这种情况下 obj.narrowBandDistance 本身就是正确值, 不需要额外处理。
float sampleSparseDistanceField(
    SparseDistanceFieldObject obj,
    StructuredBuffer<uint> brickIndirection,
    Texture3D<float> brickAtlas,
    SamplerState linearSampler,
    float3 positionOS)
{
    float3 boundsCenter = (obj.boundsMin + obj.boundsMax) * 0.5;
    float3 halfExtent = sparseDistanceFieldHalfExtentWithoutBorder(obj);
    float3 clampedPositionOS = clamp(positionOS, boundsCenter - halfExtent, boundsCenter + halfExtent);
    float distanceToClamped = length(clampedPositionOS - positionOS);
    return sampleSparseDistanceFieldClamped(obj, brickIndirection, brickAtlas, linearSampler, clampedPositionOS) + distanceToClamped;
}

// 返回 [0,1] 内相对 rayStart->rayEnd 的进出比例
float2 lineBoxIntersect(float3 rayStart, float3 rayEnd, float3 boxMin, float3 boxMax)
{
    float3 invRayDir = 1.0 / (rayEnd - rayStart);
    float3 firstPlaneIntersections = (boxMin - rayStart) * invRayDir;
    float3 secondPlaneIntersections = (boxMax - rayStart) * invRayDir;
    float3 closestPlaneIntersections = min(firstPlaneIntersections, secondPlaneIntersections);
    float3 furthestPlaneIntersections = max(firstPlaneIntersections, secondPlaneIntersections);
    float2 boxIntersections;
    boxIntersections.x = max(closestPlaneIntersections.x, max(closestPlaneIntersections.y, closestPlaneIntersections.z));
    boxIntersections.y = min(furthestPlaneIntersections.x, min(furthestPlaneIntersections.y, furthestPlaneIntersections.z));
    return saturate(boxIntersections);
}

// 跟 UE r.DistanceFields.SurfaceBiasExpand 默认值一致
static const float kMeshSDFSurfaceBiasExpand = 0.25;

// 跟 MeshDistanceField.cpp 的 kMeshDistanceFieldObjectBorder 一致
static const float kMeshDistanceFieldObjectBorder = 1.0;

// obj.boundsMin/boundsMax 是烘焙时已经含了每侧 kMeshDistanceFieldObjectBorder 个体素边界的那一份,
// 而 UE 的 ExpandSurfaceDistance/LocalToVolumeScale 取的都是外扩之前的半轴
float3 sparseDistanceFieldHalfExtentWithoutBorder(SparseDistanceFieldObject obj)
{
    float3 resolution = max(float3(obj.resolution), float3(1.0));
    return 0.5 * (obj.boundsMax - obj.boundsMin) * (resolution - 2.0 * kMeshDistanceFieldObjectBorder) / resolution;
}

float sparseDistanceFieldExpandSurfaceDistance(SparseDistanceFieldObject obj)
{
    float3 resolution = max(float3(obj.resolution), float3(1.0));
    return kMeshSDFSurfaceBiasExpand * length(sparseDistanceFieldHalfExtentWithoutBorder(obj) / resolution);
}

// 照抄 UE ShadowRayTraceThroughCulledObjects() 对单个物体的那一段(不含次表面透射), 返回可见度
float shadowRayTraceSparseDistanceFieldObject(
    SparseDistanceFieldObject obj,
    StructuredBuffer<uint> brickIndirection,
    Texture3D<float> brickAtlas,
    SamplerState linearSampler,
    float3 rayStartOS,
    float3 rayEndOS,
    float worldToLocalScale, // 按实例最大轴缩放换算
    float tanLightAngle,
    float minSphereRadius,   // 世界单位
    float maxSphereRadius,   // 世界单位
    float selfShadowBias,    // 世界单位
    bool expandSurface)
{
    float3 rayDirection = rayEndOS - rayStartOS;
    float rayLength = length(rayDirection);
    rayDirection /= rayLength;

    float3 boundsCenter = (obj.boundsMin + obj.boundsMax) * 0.5;
    // UE volume 空间的 1 个单位, 跟 LocalToVolumeScale 一样取外扩边界之前的最大半轴
    float3 halfExtent = sparseDistanceFieldHalfExtentWithoutBorder(obj);
    float volumeUnit = max(halfExtent.x, max(halfExtent.y, halfExtent.z));

    float localMinSphereRadius = minSphereRadius * worldToLocalScale;
    float localMaxSphereRadius = maxSphereRadius * worldToLocalScale;
    float selfShadowScale = 1.0 / max(selfShadowBias * worldToLocalScale, 0.0001 * volumeUnit);

    float objectCenterDistanceAlongRay = dot(boundsCenter - rayStartOS, rayDirection);
    float localConeRadiusAtObject = min(tanLightAngle * max(objectCenterDistanceAlongRay, 0.0), localMaxSphereRadius);

    float2 intersectionTimes = lineBoxIntersect(rayStartOS, rayEndOS, boundsCenter - halfExtent - localConeRadiusAtObject, boundsCenter + halfExtent + localConeRadiusAtObject);
    if (intersectionTimes.x >= intersectionTimes.y)
        return 1.0;

    // 我们的 SDF 只有一级分辨率, 按 UE 带满 DF_MAX_NUM_MIPS 级 mip 的资产处理, mip 系数为 1
    float expandSurfaceDistance = sparseDistanceFieldExpandSurfaceDistance(obj);
    float expandSurfaceFalloff = 2.0 * expandSurfaceDistance;

    float maxEncodedDistance = obj.narrowBandDistance;
    if (expandSurface)
        maxEncodedDistance -= expandSurfaceDistance;
    localMaxSphereRadius = min(localMaxSphereRadius, maxEncodedDistance);

    float sampleRayTime = intersectionTimes.x * rayLength;
    const uint kMaxSteps = 64;
    float minStepSize = volumeUnit / (4.0 * float(kMaxSteps));
    float minConeVisibility = 1.0;

    uint stepIndex = 0;
    for (; stepIndex < kMaxSteps; ++stepIndex)
    {
        float distanceField = sampleSparseDistanceField(obj, brickIndirection, brickAtlas, linearSampler, rayStartOS + rayDirection * sampleRayTime);

        if (expandSurface)
            distanceField -= expandSurfaceDistance * saturate(sampleRayTime / expandSurfaceFalloff);

        float selfShadowVisibility = 1.0 - saturate(sampleRayTime * selfShadowScale);
        float sphereRadius = clamp(tanLightAngle * sampleRayTime, localMinSphereRadius, localMaxSphereRadius);
        // sphereRadius 为 0 时取 saturate(distanceField / sphereRadius) 的极限, 直接除会得到 NaN
        float coneVisibility = sphereRadius > 0.0 ? saturate(distanceField / sphereRadius) : (distanceField > 0.0 ? 1.0 : 0.0);
        minConeVisibility = min(minConeVisibility, max(coneVisibility, selfShadowVisibility));

        sampleRayTime += max(abs(distanceField), minStepSize);

        if (minConeVisibility < 0.01 || sampleRayTime > intersectionTimes.y * rayLength)
            break;
    }

    // 步数快耗尽时强制压向遮挡
    return min(minConeVisibility, 1.0 - float(stepIndex) / float(kMaxSteps));
}

// 单个物体的精确命中查询结果——跟 shadowRayTraceSparseDistanceFieldObject() 只返回可见度
// 不同, 这里要的是命中点本身(距离+法线), 给 MDFVisualizerPass 这种要画出真实表面(而不是
// 只判断挡没挡光)的调用方用
struct SparseDistanceFieldRayHit
{
    bool hit;
    float hitDistance; // 沿 rayOriginOS + rayDirOS * t 的参数值, 局部空间单位
    float3 normalOS;
};

// 沿一条射线对单个物体的 MDF 做 sphere tracing, 步进和命中判据照抄 UE
// RayTraceThroughTileCulledDistanceFields()(DistanceFieldVisualization.usf)对单个物体的那一段
SparseDistanceFieldRayHit raymarchSparseDistanceFieldObjectPrimary(
    SparseDistanceFieldObject obj,
    StructuredBuffer<uint> brickIndirection,
    Texture3D<float> brickAtlas,
    SamplerState linearSampler,
    float3 rayOriginOS,
    float3 rayDirOS, // 必须是单位向量
    float maxDistance)
{
    SparseDistanceFieldRayHit result;
    result.hit = false;
    result.hitDistance = 0.0;
    result.normalOS = float3(0.0, 1.0, 0.0);

    float3 boundsCenter = (obj.boundsMin + obj.boundsMax) * 0.5;
    float3 halfExtent = sparseDistanceFieldHalfExtentWithoutBorder(obj);

    float3 invDir = 1.0 / rayDirOS;
    float3 t0 = (boundsCenter - halfExtent - rayOriginOS) * invDir;
    float3 t1 = (boundsCenter + halfExtent - rayOriginOS) * invDir;
    float3 tMin = min(t0, t1);
    float3 tMax = max(t0, t1);
    float tEnter = max(max(max(tMin.x, tMin.y), tMin.z), 0.0);
    float tExit = min(min(min(tMax.x, tMax.y), tMax.z), maxDistance);
    if (tExit < tEnter)
        return result;

    float3 voxelSize = (obj.boundsMax - obj.boundsMin) / max(float3(obj.resolution), float3(1.0));

    float expandSurfaceDistance = sparseDistanceFieldExpandSurfaceDistance(obj);
    float expandSurfaceFalloff = 2.0 * expandSurfaceDistance;
    float volumeUnit = max(halfExtent.x, max(halfExtent.y, halfExtent.z));

    const int kMaxSteps = 64;
    float minStepSize = volumeUnit / float(16 * kMaxSteps);

    float t = tEnter;
    bool hit = false;
    bool exitedBounds = false;
    int stepIndex = 0;

    for (; stepIndex < kMaxSteps; ++stepIndex)
    {
        float d = sampleSparseDistanceFieldClamped(obj, brickIndirection, brickAtlas, linearSampler, rayOriginOS + rayDirOS * t);
        // 起点附近不外扩, 否则紧贴射线起点的表面会被自己的外扩量吞掉
        float expandSurfaceAmount = expandSurfaceDistance * saturate(t / expandSurfaceFalloff);
        if (d < expandSurfaceAmount)
        {
            hit = true;
            t = clamp(t + d - expandSurfaceAmount, tEnter, tExit);
            break;
        }

        t += max(d, minStepSize);
        if (t > tExit + expandSurfaceAmount)
        {
            exitedBounds = true;
            break;
        }
    }

    // 步数耗尽而不是走出包围盒的照样算命中, 跟 UE RayTraceThroughTileCulledDistanceFields()
    // 的 bHit || StepIndex == MaxSteps 一致
    if (!hit && exitedBounds)
        return result;

    float3 hitPositionOS = rayOriginOS + rayDirOS * t;
    // 中心差分估法线, 故意不用解析梯度——理由见 MeshSDFScene.slang::estimateNormalOS 的注释,
    // 两边用的是同一套三线性重建, 结论同样适用。步长取半个体素(不是整个), 跟 UE
    // CalculateMeshSDFGradient()(DistanceFieldLightingShared.ush)一致: 网格只留了
    // kMeshDistanceFieldObjectBorder=1 个体素的边界, 超过半个体素往外插值就会越出这份
    // 边界, 读到边界外未定义的内容
    float3 h = max(voxelSize * 0.5, float3(1e-5));
    float dx = sampleSparseDistanceFieldClamped(obj, brickIndirection, brickAtlas, linearSampler, hitPositionOS + float3(h.x, 0.0, 0.0))
             - sampleSparseDistanceFieldClamped(obj, brickIndirection, brickAtlas, linearSampler, hitPositionOS - float3(h.x, 0.0, 0.0));
    float dy = sampleSparseDistanceFieldClamped(obj, brickIndirection, brickAtlas, linearSampler, hitPositionOS + float3(0.0, h.y, 0.0))
             - sampleSparseDistanceFieldClamped(obj, brickIndirection, brickAtlas, linearSampler, hitPositionOS - float3(0.0, h.y, 0.0));
    float dz = sampleSparseDistanceFieldClamped(obj, brickIndirection, brickAtlas, linearSampler, hitPositionOS + float3(0.0, 0.0, h.z))
             - sampleSparseDistanceFieldClamped(obj, brickIndirection, brickAtlas, linearSampler, hitPositionOS - float3(0.0, 0.0, h.z));

    result.hit = true;
    result.hitDistance = t;
    result.normalOS = normalize(float3(dx / h.x, dy / h.y, dz / h.z));
    return result;
}
