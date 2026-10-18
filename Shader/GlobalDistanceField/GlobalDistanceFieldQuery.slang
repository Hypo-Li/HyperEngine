// Global Distance Field 命中查询——第七步, 纯库函数, 由 GlobalDistanceFieldVisualizer.slang
// 调用。拆成两个独立函数(用户要求): traceGlobalDistanceField() 沿射线在 GDF 里做 sphere
// tracing 找命中点, resolveGlobalDistanceFieldHit() 从命中点反查出真正的表面归属物体、
// 混合出它的 card 数据。
//
// 核心 marching 算法照抄 UE RayTraceGlobalDistanceField(GlobalDistanceFieldUtils.ush)的
// 主干: 从最内层往外逐级 cascade 处理, 每一级算 ray-box 相交区间, 命中时间的下界
// (minRayTime)在各级之间接力传递(不管上一级有没有命中, 下一级都从上一级 box 的退出时间
// 接着走, 不从射线起点重新开始)。没有照抄的部分: mip 纹理预判、薄物体 coverage 展开、
// 抖动半透明——这些是生产级质量优化, 需要额外的 mip 链/coverage buffer, 这个引擎没有对应
// 基础设施, 硬翻译过来也用不上。
//
// resolveGlobalDistanceFieldHit() 反查用的 Object Grid(objectGrid)是按 atlas 物理槽位号
// 直接索引的一份共享 buffer(不区分 cascade/层), 跟 gdfIndirection/gdfAtlas 同样跨全部
// cascade 共享——不需要像合成阶段那样把多个 cascade 的数据合并拼进一份临时 buffer 再传,
// 也就不受"这个引擎没有 bindless buffer 数组"这条限制影响。
#pragma once
#include "../Common/Binding.slang"
#include "../Common/Math.slang"
#include "../Common/MeshShapeCard.slang"
#include "../Common/SparseDistanceField.slang"
#include "GlobalDistanceFieldObjectGrid.slang"

// 跟 hyp::kGlobalDistanceFieldCascadeCount/kGlobalDistanceFieldBricksPerAxis/
// kGlobalDistanceFieldInnermostHalfExtent/kGlobalDistanceFieldInfluenceRangeInVoxels
// (GlobalDistanceField.h)必须保持一致
static const int kGlobalDistanceFieldCascadeCount = 4;
static const int kGlobalDistanceFieldBricksPerAxis = 36;
static const float kGlobalDistanceFieldInnermostHalfExtent = 25.0;
static const float kGlobalDistanceFieldInfluenceRangeInVoxels = 4.0;

// 跟 hyp::kGlobalDistanceFieldInvalidCardRangeSlotBase(GlobalDistanceField.h)必须保持一致
static const uint kGlobalDistanceFieldInvalidCardRangeSlotBase = 0xFFFFFFFFu;

// 表面外扩量占 globalDistanceFieldVoxelExtent() 的比例——UE 开 Lumen 时这一级 cascade 分辨率
// 是 252(r.LumenScene.GlobalSDF.Resolution), 跟我们的 kGlobalDistanceFieldBricksPerAxis(36) *
// kSparseUniqueBrickSize(7) = 252 完全一致, 不是分辨率更低, 直接采用 UE 的
// CoveredExpandSurfaceScale 默认值 1.0
static const float kGlobalDistanceFieldExpandSurfaceScale = 1.0;

// MeshInstanceData 结构体定义在 Common/Binding.slang 里(跟 InstanceData/LightData 放在
// 一起, 不是 GDF 专属的东西)——Object Grid 存的 meshInstanceIndex 就是那张表的下标

// 单个 cascade 的几何信息——中心用 WorldCoord(不是 float3), 跟这个引擎的大世界坐标方案
// 保持一致(cascade 中心可能离世界原点很远)。调用方组一个 4 个元素的数组传进来(不是绑定,
// 这几个函数不持有自己的 bind group, 数据来源交给调用方)——可以直接用下面的
// globalDistanceFieldCascadeInfoAtIndex() 从相机当前位置精确重建, 不需要额外的 buffer/
// push constant
struct GlobalDistanceFieldCascadeInfo
{
    WorldCoord center;
    float halfExtent;
};

float globalDistanceFieldCellSize(GlobalDistanceFieldCascadeInfo cascade)
{
    return (2.0 * cascade.halfExtent) / float(kGlobalDistanceFieldBricksPerAxis);
}

// 半个体素的边长, 不是一整个——等价于 UE 的 ClipmapVoxelExtent(halfExtent * GlobalVolumeTexelSize,
// GlobalDistanceFieldUtils.ush), UE 侧所有"按体素算"的容差都以它为单位
float globalDistanceFieldVoxelExtent(GlobalDistanceFieldCascadeInfo cascade)
{
    return cascade.halfExtent / float(kGlobalDistanceFieldBricksPerAxis * kSparseUniqueBrickSize);
}

// 从相机当前位置精确重建第 cascadeIndex 级 cascade 的几何信息, 不需要调用方传任何 cascade
// 专属数据——halfExtent 是编译期能推的公式(kGlobalDistanceFieldInnermostHalfExtent *
// 2^cascadeIndex), center 靠 worldCoordGridSnapDelta() 在 GPU 上精确复现
// hyp::SceneProxy::update() 里 "snappedCenter = round(cameraWorldPos / cellSize)
// * cellSize" 那条对齐规则(相机移动没跨过一整格之前, 这里算出来的结果和 CPU 端存的
// cascade.center 保持一致, 不会因为在 GPU 上用 float 重算就跟合成阶段用的网格错位——
// 具体怎么在大世界坐标下精确取整见 WorldCoordinate.slang::worldCoordGridSnapDelta 的注释)
GlobalDistanceFieldCascadeInfo globalDistanceFieldCascadeInfoAtIndex(WorldCoord cameraOrigin, uint cascadeIndex)
{
    GlobalDistanceFieldCascadeInfo info;
    info.halfExtent = kGlobalDistanceFieldInnermostHalfExtent * float(1u << cascadeIndex);

    float cellSize = (2.0 * info.halfExtent) / float(kGlobalDistanceFieldBricksPerAxis);
    float3 delta = worldCoordGridSnapDelta(cameraOrigin, cellSize);

    info.center.tile = cameraOrigin.tile;
    info.center.offset = cameraOrigin.offset - delta;
    return info;
}

uint globalDistanceFieldLayerIndirectionOffset(uint cascadeIndex, bool isDynamicLayer)
{
    uint layerGlobalIndex = cascadeIndex * 2u + (isDynamicLayer ? 1u : 0u);
    return layerGlobalIndex * uint(kGlobalDistanceFieldBricksPerAxis * kGlobalDistanceFieldBricksPerAxis * kGlobalDistanceFieldBricksPerAxis);
}

// 找到从 worldPos 出发、分辨率最高但仍然"离边界至少还有一个体素余量"的那一级 cascade——
// 照抄 UE ComputeGlobalDistanceFieldClipmapIndex 的选择逻辑(Common.ush::
// ComputeDistanceFromBoxToPointInside 展开写的), 从最内层(0)往外找, 第一个满足条件的
// 就用。纯粹是给 marching 一个更好的起点(让它一开始就在尽量高分辨率的一级里走), 不是
// 正确性必需——找不到(点已经超出所有 cascade 覆盖范围)就退化用最外层
uint globalDistanceFieldFindStartCascade(GlobalDistanceFieldCascadeInfo cascades[kGlobalDistanceFieldCascadeCount], WorldCoord worldPos)
{
    for (uint i = 0; i < uint(kGlobalDistanceFieldCascadeCount); ++i)
    {
        float3 relPos = calculateRelativeWorldPosition(worldPos, cascades[i].center);
        float voxelExtent = globalDistanceFieldVoxelExtent(cascades[i]);

        float3 distToMin = max(relPos + cascades[i].halfExtent, float3(0.0));
        float3 distToMax = max(cascades[i].halfExtent - relPos, float3(0.0));
        float3 closest = min(distToMin, distToMax);
        float margin = min(closest.x, min(closest.y, closest.z));

        if (margin > voxelExtent)
            return i;
    }
    return uint(kGlobalDistanceFieldCascadeCount) - 1u;
}

// UE ClipRayToStartOutsideGlobalSDF(GlobalDistanceFieldUtils.ush:202-221)——把射线起点推到
// 最外层 cascade 包围盒的出口, 缩短 rayLength 到盒子外剩下的部分, 给反射 R7 的远距离屏幕
// 追踪当起点(GDF 自身已经不覆盖的地方才需要屏幕追踪兜底)。射线跟盒子不相交(已经在盒子外
// 朝外走)时原样不动
void clipRayToStartOutsideGlobalDistanceField(
    in GlobalDistanceFieldCascadeInfo outerCascade, inout WorldCoord rayOrigin, float3 rayDirectionWS, inout float rayLength)
{
    float3 originRel = calculateRelativeWorldPosition(rayOrigin, outerCascade.center);
    float3 rayEndRel = originRel + rayDirectionWS * rayLength;
    float boxExtent = outerCascade.halfExtent - globalDistanceFieldVoxelExtent(outerCascade);
    float2 intersectionTimes = lineBoxIntersect(originRel, rayEndRel, float3(-boxExtent), float3(boxExtent));

    if (intersectionTimes.x < intersectionTimes.y)
    {
        // 出口时间往回收一点点, 抵消抖动步进偏移在盒子边界留下的间隙, 假设 16 步(UE 同一个系数)
        float exitTime = max((32.0 / 31.0) * intersectionTimes.y - 1.0 / 31.0, 0.0);
        rayOrigin.offset += rayDirectionWS * (exitTime * rayLength);
        rayLength = max(rayLength * (1.0 - exitTime), 0.0);
    }
}

// 采样已经合成好的 GDF 结果(动态层, 已经把静态层结果合并进去了, 见
// GlobalDistanceFieldComposite.slang 顶部注释)——samplePosRel 是相对这一级 cascade 中心
// 的位置。查不到 brick 时返回的不是真实距离值, 而是这一点到任何表面距离的保守下界
float sampleGlobalDistanceFieldCascade(
    GlobalDistanceFieldCascadeInfo cascade,
    StructuredBuffer<uint> gdfIndirection,
    Texture3D<float> gdfAtlas,
    SamplerState linearSampler,
    int gdfAtlasBrickGridExtent,
    uint layerIndirectionOffset,
    float3 samplePosRel)
{
    float cellSize = globalDistanceFieldCellSize(cascade);

    float3 denseVoxelCoord = (samplePosRel + cascade.halfExtent) / cellSize * float(kSparseUniqueBrickSize);
    float3 clampedVoxelCoord = clamp(
        denseVoxelCoord,
        float3(0.0),
        float3(float(kGlobalDistanceFieldBricksPerAxis * kSparseUniqueBrickSize))
    );

    int3 cellCoord = clamp(
        int3(floor(clampedVoxelCoord / float(kSparseUniqueBrickSize))),
        int3(0),
        int3(kGlobalDistanceFieldBricksPerAxis - 1)
    );
    uint cellIndex = (uint(cellCoord.z) * uint(kGlobalDistanceFieldBricksPerAxis) + uint(cellCoord.y)) * uint(kGlobalDistanceFieldBricksPerAxis) + uint(cellCoord.x);

    uint slot = gdfIndirection[layerIndirectionOffset + cellIndex];
    if (slot == kInvalidSparseBrickIndex)
        return kGlobalDistanceFieldInfluenceRangeInVoxels * cellSize / float(kSparseUniqueBrickSize);

    float3 atlasOrigin = float3(sparseBrickSlotToAtlasOrigin(slot, gdfAtlasBrickGridExtent));
    float3 localOffset = clampedVoxelCoord - float3(cellCoord) * float(kSparseUniqueBrickSize);
    float3 atlasVoxel = atlasOrigin + localOffset;

    float3 atlasExtent = float3((float)(gdfAtlasBrickGridExtent * kSparseBrickSize));
    float3 uvw = (atlasVoxel + 0.5) / atlasExtent;
    float encoded = gdfAtlas.SampleLevel(linearSampler, uvw, 0.0);
    float influenceRange = kGlobalDistanceFieldInfluenceRangeInVoxels * cellSize / float(kSparseUniqueBrickSize);
    return decodeSignedDistance(encoded, influenceRange);
}

struct GlobalDistanceFieldTraceResult
{
    bool hit;
    float hitDistance; // 沿射线的距离, rayOrigin + rayDirection * hitDistance = 命中点
    uint hitCascadeIndex;
};

// 跟 UE FGlobalSDFTraceInput 逐项对应
struct GlobalDistanceFieldTraceInput
{
    WorldCoord rayOrigin;
    float3 rayDirection; // 必须是单位向量
    float minTraceDistance;
    float maxTraceDistance;
    float stepFactor;
    float minStepFactor;
    // true: 外扩量随离开起点的射线时间增长(宁可多遮挡); false: 随射线上见过的最大距离场值增长(更少误自交)
    bool expandSurfaceUsingRayTimeInsteadOfMaxDistance;
    float initialMaxDistance;
    // 以当前 cascade 的 voxelExtent 为单位
    float voxelSizeRelativeBias;
    float voxelSizeRelativeRayEndBias;
};

// 跟 UE SetupGlobalSDFTraceInput() 的默认值一致
GlobalDistanceFieldTraceInput setupGlobalDistanceFieldTraceInput(WorldCoord rayOrigin, float3 rayDirection, float minTraceDistance, float maxTraceDistance, float stepFactor, float minStepFactor)
{
    GlobalDistanceFieldTraceInput input;
    input.rayOrigin = rayOrigin;
    input.rayDirection = rayDirection;
    input.minTraceDistance = minTraceDistance;
    input.maxTraceDistance = maxTraceDistance;
    input.stepFactor = stepFactor;
    input.minStepFactor = minStepFactor;
    input.expandSurfaceUsingRayTimeInsteadOfMaxDistance = true;
    input.initialMaxDistance = 0.0;
    input.voxelSizeRelativeBias = 0.0;
    input.voxelSizeRelativeRayEndBias = 0.0;
    return input;
}

// 沿一条射线做 sphere tracing, 找 GDF(动态层, 已合并静态层)里的第一个命中点——见文件
// 顶部注释, 核心结构照抄 UE RayTraceGlobalDistanceField 的主干(去掉 mip/coverage)
GlobalDistanceFieldTraceResult traceGlobalDistanceField(
    GlobalDistanceFieldCascadeInfo cascades[kGlobalDistanceFieldCascadeCount],
    StructuredBuffer<uint> gdfIndirection,
    Texture3D<float> gdfAtlas,
    SamplerState linearSampler,
    int gdfAtlasBrickGridExtent,
    GlobalDistanceFieldTraceInput input)
{
    GlobalDistanceFieldTraceResult result;
    result.hit = false;
    result.hitDistance = 0.0;
    result.hitCascadeIndex = 0;

    WorldCoord rayOrigin = input.rayOrigin;
    float3 rayDirection = input.rayDirection;
    WorldCoord minTracePosition = rayOrigin;
    minTracePosition.offset += rayDirection * input.minTraceDistance;
    uint startCascade = globalDistanceFieldFindStartCascade(cascades, minTracePosition);
    float minRayTime = input.minTraceDistance;
    float maxDistance = input.initialMaxDistance;

    for (uint cascadeIndex = startCascade; cascadeIndex < uint(kGlobalDistanceFieldCascadeCount) && !result.hit; ++cascadeIndex)
    {
        GlobalDistanceFieldCascadeInfo cascade = cascades[cascadeIndex];
        float voxelExtent = globalDistanceFieldVoxelExtent(cascade);
        float minStepSize = input.minStepFactor * voxelExtent;
        float expandSurfaceDistance = voxelExtent;
        float clipmapRayBias = voxelExtent * input.voxelSizeRelativeBias;
        float clipmapRayLength = input.maxTraceDistance - voxelExtent * input.voxelSizeRelativeRayEndBias;

        float3 rayOriginRel = calculateRelativeWorldPosition(rayOrigin, cascade.center);

        // 贴边采样会插值到 cascade 外不存在的体素, 所以往里收一个 voxelExtent
        float traceHalfExtent = cascade.halfExtent - voxelExtent;
        float3 invDir = 1.0 / rayDirection;
        float3 t0 = (-traceHalfExtent - rayOriginRel) * invDir;
        float3 t1 = (traceHalfExtent - rayOriginRel) * invDir;
        float3 tMin = min(t0, t1);
        float3 tMax = max(t0, t1);
        float boxEnter = max(max(tMin.x, tMin.y), tMin.z);
        float boxExit = min(min(tMax.x, tMax.y), tMax.z);

        boxEnter = max(max(boxEnter, minRayTime), clipmapRayBias);
        boxExit = min(boxExit, clipmapRayLength);

        if (boxEnter < boxExit)
        {
            // 不管这一级有没有命中, 下一级都从这一级 box 的退出时间接着走, 不从射线起点
            // 重新开始——这是让 marching 能跨 cascade 连续推进、而不是每一级都重新扫一遍
            // 已经扫过的近处空间的关键
            minRayTime = boxExit;

            uint layerOffset = globalDistanceFieldLayerIndirectionOffset(cascadeIndex, true);
            float t = boxEnter;
            const uint kMaxSteps = 256;
            for (uint step = 0; step < kMaxSteps; ++step)
            {
                float3 samplePosRel = rayOriginRel + rayDirection * t;
                float dist = sampleGlobalDistanceFieldCascade(cascade, gdfIndirection, gdfAtlas, linearSampler, gdfAtlasBrickGridExtent, layerOffset, samplePosRel);

                maxDistance = max(dist, maxDistance);
                float expandSurfaceTime = input.expandSurfaceUsingRayTimeInsteadOfMaxDistance ? t - clipmapRayBias : maxDistance;
                float hitThreshold = expandSurfaceDistance * saturate(expandSurfaceTime / (2.0 * expandSurfaceDistance)) * kGlobalDistanceFieldExpandSurfaceScale;

                if (dist < hitThreshold)
                {
                    // hitThreshold 不减掉的话, 报出来的表面位置会外扩 hitThreshold 那么多
                    float hitTime = t + dist - hitThreshold;

                    result.hit = true;
                    result.hitDistance = max(hitTime, 0.0);
                    result.hitCascadeIndex = cascadeIndex;
                    break;
                }

                t += max(dist * input.stepFactor, minStepSize);
                if (t > boxExit)
                    break;
            }
        }
    }

    return result;
}

struct GlobalDistanceFieldResolveResult
{
    bool valid;
    float3 normalWS;   // GDF 自身梯度的中心差分(MDF 路径填的是精确求交法线, 见
                        // resolveMdfHit())
    bool cardHit;              // false 表示没有任何一张 card 覆盖到这个点, 只有 color 有意义
    float3 cardBlendedNormalWS; // 各张覆盖到这个点的 card 在 NormalAtlas 上的采样值按权重
                                // 混合、再变换到世界空间的结果
    float3 cardAlbedo;
    float3 cardLighting;        // FinalLightingAtlas 的加权混合结果(已经是 albedo/PI * direct + emissive)
    float3 cardEmissive;
    float3 cardDirectLighting;   // DirectLightingAtlas 原始加权混合结果, 不乘 albedo
    float3 cardIndirectLighting; // IndirectLightingAtlas 原始加权混合结果, 不乘 albedo
    // 全部候选卡权重之和, 归一化之前的值——单张卡满权重是 1, 同方向的多层卡都命中时会超过 1
    float cardWeightSum;
    float cardAxisWeightSum;
    float3 cardWeightsColor;
    float3 color;               // cardHit 为 false 时给可视化用的标注色: 这个实例完全没有
                                // card 数据是品红, 有 card 数据但没有一张覆盖到是黄色
    // 只有 traceMeshSDFNearField() 会填这个字段——resolveGlobalDistanceFieldHit()/resolveMdfHit()
    // 调用时命中距离已经由调用方在 GDF marching 里算出来了, 不需要这个函数再报一次
    float hitDistance;
};

// 按 Object Grid 里离表面由近到远的候选物体逐个累加 card 采样
GlobalDistanceFieldResolveResult resolveGlobalDistanceFieldHit(
    GlobalDistanceFieldCascadeInfo cascade,
    uint hitCascadeIndex,
    WorldCoord hitWorldPos,
    StructuredBuffer<uint> gdfIndirection,
    Texture3D<float> gdfAtlas,
    int gdfAtlasBrickGridExtent,
    float3 rayDirectionWS, // 梯度退化时的兜底法线取它的反向
    StructuredBuffer<uint> objectGrid,
    StructuredBuffer<MeshInstanceData> meshInstanceData,
    StructuredBuffer<uint> meshShapeData,
    SamplerState linearSampler,
    StructuredBuffer<uint> cardPages,
    Texture2D<float4> cardFinalLightingAtlas,
    Texture2D<float4> cardAlbedoAtlas,
    Texture2D<float4> cardNormalAtlas,
    Texture2D<float4> cardEmissiveAtlas,
    Texture2D<float4> cardDirectLightingAtlas,
    Texture2D<float4> cardIndirectLightingAtlas,
    Texture2D<float> cardDepthAtlas)
{
    GlobalDistanceFieldResolveResult result;
    result.valid = false;
    result.normalWS = float3(0.0, 0.0, 1.0);
    result.cardHit = false;
    result.cardBlendedNormalWS = float3(0.0, 0.0, 1.0);
    result.cardAlbedo = float3(0.0);
    result.cardLighting = float3(0.0);
    result.cardEmissive = float3(0.0);
    result.cardDirectLighting = float3(0.0);
    result.cardIndirectLighting = float3(0.0);
    result.cardWeightSum = 0.0;
    result.cardAxisWeightSum = 0.0;
    result.cardWeightsColor = float3(0.0);
    result.color = float3(1.0, 0.0, 1.0); // 品红: 这个实例还没有可用的 card 数据
    result.hitDistance = 0.0;

    float3 posRel = calculateRelativeWorldPosition(hitWorldPos, cascade.center);
    float cellSize = globalDistanceFieldCellSize(cascade);
    float voxelExtent = globalDistanceFieldVoxelExtent(cascade);

    // 法线必须取自 trace 用的同一个场——per-mesh SDF 的零等值面跟这里不重合, 命中点在它
    // 的窄带外面时值被钳成常数, 梯度是退化的
    uint dynamicLayerOffset = globalDistanceFieldLayerIndirectionOffset(hitCascadeIndex, true);
    float distPosX = sampleGlobalDistanceFieldCascade(cascade, gdfIndirection, gdfAtlas, linearSampler, gdfAtlasBrickGridExtent, dynamicLayerOffset, posRel + float3(voxelExtent, 0.0, 0.0));
    float distNegX = sampleGlobalDistanceFieldCascade(cascade, gdfIndirection, gdfAtlas, linearSampler, gdfAtlasBrickGridExtent, dynamicLayerOffset, posRel - float3(voxelExtent, 0.0, 0.0));
    float distPosY = sampleGlobalDistanceFieldCascade(cascade, gdfIndirection, gdfAtlas, linearSampler, gdfAtlasBrickGridExtent, dynamicLayerOffset, posRel + float3(0.0, voxelExtent, 0.0));
    float distNegY = sampleGlobalDistanceFieldCascade(cascade, gdfIndirection, gdfAtlas, linearSampler, gdfAtlasBrickGridExtent, dynamicLayerOffset, posRel - float3(0.0, voxelExtent, 0.0));
    float distPosZ = sampleGlobalDistanceFieldCascade(cascade, gdfIndirection, gdfAtlas, linearSampler, gdfAtlasBrickGridExtent, dynamicLayerOffset, posRel + float3(0.0, 0.0, voxelExtent));
    float distNegZ = sampleGlobalDistanceFieldCascade(cascade, gdfIndirection, gdfAtlas, linearSampler, gdfAtlasBrickGridExtent, dynamicLayerOffset, posRel - float3(0.0, 0.0, voxelExtent));
    float3 gradient = float3(distPosX - distNegX, distPosY - distNegY, distPosZ - distNegZ);
    float gradientLength = length(gradient);
    bool gradientValid = gradientLength > 1e-3;
    result.normalWS = gradientValid ? gradient / gradientLength : -rayDirectionWS;

    // trace 报告的命中点在外扩表面上, 不在零等值面上
    float distAtHit = (distPosX + distNegX + distPosY + distNegY + distPosZ + distNegZ) / 6.0;
    WorldCoord surfaceWorldPos = hitWorldPos;
    if (gradientValid)
        surfaceWorldPos.offset -= result.normalWS * distAtHit;

    // 命中点正好压在表面上, 落进哪一格取决于浮点舍入, 所以沿法线外推一个体素再查
    // Object Grid 第 g 格的候选距离在 localId=2g 的体素处采样, 偏移半个体素让该点距所读区域中心不超过 0.375 个体素
    float3 gridPosRel = posRel + result.normalWS * voxelExtent + float3(voxelExtent);
    int3 cellCoord = clamp(
        int3(floor((gridPosRel + cascade.halfExtent) / cellSize)),
        int3(0),
        int3(kGlobalDistanceFieldBricksPerAxis - 1)
    );
    uint cellIndex = (uint(cellCoord.z) * uint(kGlobalDistanceFieldBricksPerAxis) + uint(cellCoord.y)) * uint(kGlobalDistanceFieldBricksPerAxis) + uint(cellCoord.x);

    // 动态层的 Object Grid 在合成时已经并入了静态层同一格的候选
    uint slot = gdfIndirection[dynamicLayerOffset + cellIndex];
    if (slot == kInvalidSparseBrickIndex)
        return result;

    float3 coordInBrick = (gridPosRel + cascade.halfExtent) / cellSize - float3(cellCoord);
    uint3 objectGridCellCoord = uint3(clamp(
        int3(floor(coordInBrick * float(kGlobalDistanceFieldObjectGridCellsPerBrickAxis))),
        int3(0),
        int3(kGlobalDistanceFieldObjectGridCellsPerBrickAxis - 1)));
    uint entryOffset = globalDistanceFieldObjectGridEntryOffset(slot, globalDistanceFieldObjectGridCellLinearIndex(objectGridCellCoord));

    float surfaceCacheBias = kGlobalDistanceFieldObjectGridCardInterpolationRangeInVoxels * voxelExtent;
    bool anyCardData = false;
    MeshShapeCardSampleAccumulator total;
    total.albedoSum = float3(0.0);
    total.normalSum = float3(0.0);
    total.lightingSum = float3(0.0);
    total.emissiveSum = float3(0.0);
    total.directLightingSum = float3(0.0);
    total.indirectLightingSum = float3(0.0);
    total.weightSum = 0.0;
    total.axisWeightSum = 0.0;
    total.debugColorSum = float3(0.0);

    for (uint k = 0; k < kGlobalDistanceFieldObjectGridCellCapacity; ++k)
    {
        uint entry = objectGrid[entryOffset + k];
        if (entry == kGlobalDistanceFieldObjectGridInvalidEntry)
            continue;

        result.valid = true;

        MeshInstanceData candidateMeshInstance = meshInstanceData[unpackGlobalDistanceFieldObjectGridMeshInstanceIndex(entry)];
        if (candidateMeshInstance.cardRangeSlotBase == kGlobalDistanceFieldInvalidCardRangeSlotBase)
            continue;
        anyCardData = true;

        InstanceData instanceData = sInstanceData[candidateMeshInstance.instanceIndex];
        WorldCoord objTranslation = instanceData.objectToWorld.getTranslation();
        float3 positionOS = mul(calculateRelativeWorldPosition(surfaceWorldPos, objTranslation), instanceData.worldToObjectRS);
        // 法线变回局部空间要用 transpose(RS) 而不是 worldToObjectRS(=inverse(RS)), mul(M, v) 即是
        float3 normalOS = normalize(mul(instanceData.objectToWorldRS, result.normalWS));

        uint meshShapeDataOffset = candidateMeshInstance.meshShapeDataOffset;
        MeshShapeCardSampleAccumulator acc = sampleMeshShapeSurfaceCache(
            meshShapeData, cardPages,
            cardFinalLightingAtlas, cardAlbedoAtlas, cardNormalAtlas,
            cardEmissiveAtlas, cardDirectLightingAtlas, cardIndirectLightingAtlas, cardDepthAtlas,
            meshShapeDataOffset, loadMeshShapeResolution(meshShapeData, meshShapeDataOffset),
            int(loadMeshShapeCardCount(meshShapeData, meshShapeDataOffset)),
            candidateMeshInstance.cardRangeSlotBase, positionOS, normalOS, surfaceCacheBias,
            instanceWorldScale(instanceData));

        total.albedoSum += acc.albedoSum;
        total.normalSum += mul(acc.normalSum, instanceData.objectToWorldNormalRot);
        total.lightingSum += acc.lightingSum;
        total.emissiveSum += acc.emissiveSum;
        total.directLightingSum += acc.directLightingSum;
        total.indirectLightingSum += acc.indirectLightingSum;
        total.weightSum += acc.weightSum;
        total.axisWeightSum += acc.axisWeightSum;
        total.debugColorSum += acc.debugColorSum;

        // 跟 UE EvaluateGlobalDistanceFieldHit() 的 SampleWeightSum 提前退出阈值一致
        if (total.weightSum >= 0.9)
            break;
    }

    if (!anyCardData)
        return result;

    result.cardWeightSum = total.weightSum;
    result.cardAxisWeightSum = total.axisWeightSum;
    result.cardHit = total.weightSum >= 1e-4;
    if (result.cardHit)
    {
        float invWeightSum = 1.0 / total.weightSum;
        result.cardAlbedo = total.albedoSum * invWeightSum;
        result.cardLighting = total.lightingSum * invWeightSum;
        result.cardEmissive = total.emissiveSum * invWeightSum;
        result.cardDirectLighting = total.directLightingSum * invWeightSum;
        result.cardIndirectLighting = total.indirectLightingSum * invWeightSum;
        result.cardWeightsColor = total.debugColorSum * invWeightSum;

        float blendedLength = length(total.normalSum);
        if (blendedLength > 1e-6)
            result.cardBlendedNormalWS = total.normalSum / blendedLength;
    }
    else
    {
        result.color = float3(1.0, 1.0, 0.0);
    }

    return result;
}

// 跟 resolveGlobalDistanceFieldHit() 共用同一套"从命中点找 GDF 梯度法线、外推半个体素、
// 查 Object Grid cell"的候选发现逻辑(见该函数注释, 这里到候选循环开始为止的一段是它的
// 精简重复, 因为这里只是借 GDF 场找 Object Grid cell, 不需要 GDF 自身的法线/表面位置)。
// 区别在候选到手之后: 不像 GDF 路径那样把 cell 里的全部候选按权重混合, 而是对每个候选
// 物体自己原始的 per-mesh SDF(mdfBrickAtlas, 不是 gdfAtlas)沿这条相机射线做精确 sphere
// tracing(raymarchSparseDistanceFieldObjectPrimary()), 取沿射线命中时间最近的那个真实交点
// 作为唯一赢家, 只对这一个实例采样 Surface Cache——用于对比"GDF 体素化合成引入的表面误差
// 有多大", 也是 hyp::SceneVisualizeMode 在 MDF 追踪模式下的着色来源, 见
// GlobalDistanceFieldVisualizer.slang。找不到任何候选或者所有候选都没有精确命中时
// valid=false, 调用方按"这一带 GDF 认为有表面但 MDF 精确追踪没追上"处理(候选列表本身可能
// 缺失, 或者 GDF 报的命中点离候选物体实际表面已经远到不在这条射线的候选查询范围内)
GlobalDistanceFieldResolveResult resolveMdfHit(
    GlobalDistanceFieldCascadeInfo cascade,
    uint hitCascadeIndex,
    WorldCoord hitWorldPos,
    StructuredBuffer<uint> gdfIndirection,
    Texture3D<float> gdfAtlas,
    int gdfAtlasBrickGridExtent,
    SamplerState linearSampler,
    WorldCoord rayOriginWS,
    float3 rayDirectionWS,
    float rayMaxDistance,
    StructuredBuffer<uint> objectGrid,
    StructuredBuffer<MeshInstanceData> meshInstanceData,
    StructuredBuffer<uint> meshShapeData,
    Texture3D<float> mdfBrickAtlas,
    int mdfAtlasBrickGridExtent,
    StructuredBuffer<uint> cardPages,
    Texture2D<float4> cardFinalLightingAtlas,
    Texture2D<float4> cardAlbedoAtlas,
    Texture2D<float4> cardNormalAtlas,
    Texture2D<float4> cardEmissiveAtlas,
    Texture2D<float4> cardDirectLightingAtlas,
    Texture2D<float4> cardIndirectLightingAtlas,
    Texture2D<float> cardDepthAtlas)
{
    GlobalDistanceFieldResolveResult result;
    result.valid = false;
    result.normalWS = float3(0.0, 0.0, 1.0);
    result.cardHit = false;
    result.cardBlendedNormalWS = float3(0.0, 0.0, 1.0);
    result.cardAlbedo = float3(0.0);
    result.cardLighting = float3(0.0);
    result.cardEmissive = float3(0.0);
    result.cardDirectLighting = float3(0.0);
    result.cardIndirectLighting = float3(0.0);
    result.cardWeightSum = 0.0;
    result.cardAxisWeightSum = 0.0;
    result.cardWeightsColor = float3(0.0);
    result.color = float3(1.0, 0.0, 1.0); // 品红: 没有精确命中(GDF 认为有表面但 MDF 追踪没追上)
    result.hitDistance = 0.0;

    float3 posRel = calculateRelativeWorldPosition(hitWorldPos, cascade.center);
    float cellSize = globalDistanceFieldCellSize(cascade);
    float voxelExtent = globalDistanceFieldVoxelExtent(cascade);

    uint dynamicLayerOffset = globalDistanceFieldLayerIndirectionOffset(hitCascadeIndex, true);
    float distPosX = sampleGlobalDistanceFieldCascade(cascade, gdfIndirection, gdfAtlas, linearSampler, gdfAtlasBrickGridExtent, dynamicLayerOffset, posRel + float3(voxelExtent, 0.0, 0.0));
    float distNegX = sampleGlobalDistanceFieldCascade(cascade, gdfIndirection, gdfAtlas, linearSampler, gdfAtlasBrickGridExtent, dynamicLayerOffset, posRel - float3(voxelExtent, 0.0, 0.0));
    float distPosY = sampleGlobalDistanceFieldCascade(cascade, gdfIndirection, gdfAtlas, linearSampler, gdfAtlasBrickGridExtent, dynamicLayerOffset, posRel + float3(0.0, voxelExtent, 0.0));
    float distNegY = sampleGlobalDistanceFieldCascade(cascade, gdfIndirection, gdfAtlas, linearSampler, gdfAtlasBrickGridExtent, dynamicLayerOffset, posRel - float3(0.0, voxelExtent, 0.0));
    float distPosZ = sampleGlobalDistanceFieldCascade(cascade, gdfIndirection, gdfAtlas, linearSampler, gdfAtlasBrickGridExtent, dynamicLayerOffset, posRel + float3(0.0, 0.0, voxelExtent));
    float distNegZ = sampleGlobalDistanceFieldCascade(cascade, gdfIndirection, gdfAtlas, linearSampler, gdfAtlasBrickGridExtent, dynamicLayerOffset, posRel - float3(0.0, 0.0, voxelExtent));
    float3 gradient = float3(distPosX - distNegX, distPosY - distNegY, distPosZ - distNegZ);
    float gradientLength = length(gradient);
    float3 gdfNormalWS = gradientLength > 1e-3 ? gradient / gradientLength : -rayDirectionWS;

    float3 gridPosRel = posRel + gdfNormalWS * voxelExtent + float3(voxelExtent);
    int3 cellCoord = clamp(
        int3(floor((gridPosRel + cascade.halfExtent) / cellSize)),
        int3(0),
        int3(kGlobalDistanceFieldBricksPerAxis - 1)
    );
    uint cellIndex = (uint(cellCoord.z) * uint(kGlobalDistanceFieldBricksPerAxis) + uint(cellCoord.y)) * uint(kGlobalDistanceFieldBricksPerAxis) + uint(cellCoord.x);

    uint slot = gdfIndirection[dynamicLayerOffset + cellIndex];
    if (slot == kInvalidSparseBrickIndex)
        return result;

    float3 coordInBrick = (gridPosRel + cascade.halfExtent) / cellSize - float3(cellCoord);
    uint3 objectGridCellCoord = uint3(clamp(
        int3(floor(coordInBrick * float(kGlobalDistanceFieldObjectGridCellsPerBrickAxis))),
        int3(0),
        int3(kGlobalDistanceFieldObjectGridCellsPerBrickAxis - 1)));
    uint entryOffset = globalDistanceFieldObjectGridEntryOffset(slot, globalDistanceFieldObjectGridCellLinearIndex(objectGridCellCoord));

    bool anyHit = false;
    float closestHitDistanceWS = rayMaxDistance;
    float3 bestNormalOS = float3(0.0, 1.0, 0.0);
    float3 bestPositionOS = float3(0.0);
    MeshInstanceData bestMeshInstance;
    InstanceData bestInstanceData;

    for (uint k = 0; k < kGlobalDistanceFieldObjectGridCellCapacity; ++k)
    {
        uint entry = objectGrid[entryOffset + k];
        if (entry == kGlobalDistanceFieldObjectGridInvalidEntry)
            continue;

        result.valid = true;

        MeshInstanceData candidateMeshInstance = meshInstanceData[unpackGlobalDistanceFieldObjectGridMeshInstanceIndex(entry)];
        InstanceData instanceData = sInstanceData[candidateMeshInstance.instanceIndex];

        SparseDistanceFieldObject obj = loadSparseDistanceFieldObject(meshShapeData, candidateMeshInstance.meshShapeDataOffset, mdfAtlasBrickGridExtent);

        WorldCoord objTranslation = instanceData.objectToWorld.getTranslation();
        float3 rayOriginRel = calculateRelativeWorldPosition(rayOriginWS, objTranslation);
        float3 rayOriginOS = mul(rayOriginRel, instanceData.worldToObjectRS);

        // 归一化之前的长度就是这个方向上"物体空间单位/世界单位"的比例, 对非均匀缩放同样精确
        float3 rayDirOSUnnormalized = mul(rayDirectionWS, instanceData.worldToObjectRS);
        float objectUnitsPerWorldUnit = max(length(rayDirOSUnnormalized), 1e-8);
        float3 rayDirOS = rayDirOSUnnormalized / objectUnitsPerWorldUnit;
        float localMaxDistance = rayMaxDistance * objectUnitsPerWorldUnit;

        SparseDistanceFieldRayHit hit = raymarchSparseDistanceFieldObjectPrimary(
            obj, meshShapeData, mdfBrickAtlas, linearSampler, rayOriginOS, rayDirOS, localMaxDistance);
        if (!hit.hit)
            continue;

        float hitDistanceWS = hit.hitDistance / objectUnitsPerWorldUnit;
        if (hitDistanceWS < closestHitDistanceWS)
        {
            anyHit = true;
            closestHitDistanceWS = hitDistanceWS;
            bestNormalOS = hit.normalOS;
            bestPositionOS = rayOriginOS + rayDirOS * hit.hitDistance;
            bestMeshInstance = candidateMeshInstance;
            bestInstanceData = instanceData;
        }
    }

    if (!anyHit)
    {
        // GDF 粗测距认为这一带有表面, 但候选物体的精确 MDF 追踪没有真正命中(典型出现在
        // 视轮廓边缘, 见 GDF 表面外扩量在掠射角被放大的解释)——这不是"命中了但没有 card
        // 数据"(品红), 是根本没有精确命中, 必须清掉循环里为每个候选设过的 valid=true,
        // 让调用方按纯背景处理, 不产生一圈品红标注色
        result.valid = false;
        return result;
    }

    // 跟 resolveGlobalDistanceFieldHit() 里 acc.normalSum 变换回世界空间同一个约定:
    // mul(法线, objectToWorldNormalRot), 不是 mul(objectToWorldRS, 法线)
    result.normalWS = normalize(mul(bestNormalOS, bestInstanceData.objectToWorldNormalRot));

    if (bestMeshInstance.cardRangeSlotBase == kGlobalDistanceFieldInvalidCardRangeSlotBase)
        return result; // 精确命中了, 但这个实例还没有可用的 card 数据(品红)

    float surfaceCacheBias = kGlobalDistanceFieldObjectGridCardInterpolationRangeInVoxels * voxelExtent;
    uint meshShapeDataOffset = bestMeshInstance.meshShapeDataOffset;
    MeshShapeCardSampleAccumulator acc = sampleMeshShapeSurfaceCache(
        meshShapeData, cardPages,
        cardFinalLightingAtlas, cardAlbedoAtlas, cardNormalAtlas,
        cardEmissiveAtlas, cardDirectLightingAtlas, cardIndirectLightingAtlas, cardDepthAtlas,
        meshShapeDataOffset, loadMeshShapeResolution(meshShapeData, meshShapeDataOffset),
        int(loadMeshShapeCardCount(meshShapeData, meshShapeDataOffset)),
        bestMeshInstance.cardRangeSlotBase, bestPositionOS, bestNormalOS, surfaceCacheBias,
        instanceWorldScale(bestInstanceData));

    result.cardWeightSum = acc.weightSum;
    result.cardAxisWeightSum = acc.axisWeightSum;
    result.cardHit = acc.weightSum >= 1e-4;
    if (result.cardHit)
    {
        float invWeightSum = 1.0 / acc.weightSum;
        result.cardAlbedo = acc.albedoSum * invWeightSum;
        result.cardLighting = acc.lightingSum * invWeightSum;
        result.cardEmissive = acc.emissiveSum * invWeightSum;
        result.cardDirectLighting = acc.directLightingSum * invWeightSum;
        result.cardIndirectLighting = acc.indirectLightingSum * invWeightSum;
        result.cardWeightsColor = acc.debugColorSum * invWeightSum;

        float3 normalSumWS = mul(acc.normalSum, bestInstanceData.objectToWorldNormalRot);
        float blendedLength = length(normalSumWS);
        result.cardBlendedNormalWS = blendedLength > 1e-6 ? normalSumWS / blendedLength : result.normalWS;
    }
    else
    {
        result.color = float3(1.0, 1.0, 0.0);
    }

    return result;
}

// M3c(Mesh SDF 近场): 沿射线步进查询 Object Grid, 收集去重后的候选物体列表, 逐个用
// raymarchSparseDistanceFieldObjectPrimary() 精确求交, 取沿射线最近的命中——跟上面
// resolveMdfHit() 共享"从已知世界坐标反查 Object Grid cell、对候选做精确 SDF 求交、赢家采
// Surface Cache"这段逻辑(候选到手之后的尾段基本是它的重复), 区别只在候选来源: resolveMdfHit()
// 从一个已经算出来的 GDF 命中点反查一次 cell, 这里没有已知命中点, 要自己沿射线走多个 cell
// 收集候选。见 Document/Lumen_ScreenProbeGather_Plan.md §6.1、§11 #7(已拍板选 A)。
//
// 候选发现只在起点所在的这一级 cascade 内做(近场追踪距离 1.8m 远小于任何一级 cascade 的
// halfExtent, 一次近场追踪不会跨 cascade), 步长取 Object Grid cell 半边长
// (globalDistanceFieldObjectGridCellHalfExtent), 保证不会跳过任何一个 cell。
static const uint kMeshSDFNearFieldMaxCandidates = 8;
static const uint kMeshSDFNearFieldMaxSteps = 16;

GlobalDistanceFieldResolveResult traceMeshSDFNearField(
    GlobalDistanceFieldCascadeInfo cascades[kGlobalDistanceFieldCascadeCount],
    StructuredBuffer<uint> gdfIndirection,
    StructuredBuffer<uint> objectGrid,
    StructuredBuffer<MeshInstanceData> meshInstanceData,
    StructuredBuffer<uint> meshShapeData,
    Texture3D<float> mdfBrickAtlas,
    int mdfAtlasBrickGridExtent,
    SamplerState linearSampler,
    WorldCoord rayOriginWS,
    float3 rayDirectionWS,
    float minTraceDistance,
    float maxTraceDistance,
    StructuredBuffer<uint> cardPages,
    Texture2D<float4> cardFinalLightingAtlas,
    Texture2D<float4> cardAlbedoAtlas,
    Texture2D<float4> cardNormalAtlas,
    Texture2D<float4> cardEmissiveAtlas,
    Texture2D<float4> cardDirectLightingAtlas,
    Texture2D<float4> cardIndirectLightingAtlas,
    Texture2D<float> cardDepthAtlas)
{
    GlobalDistanceFieldResolveResult result;
    result.valid = false;
    result.normalWS = float3(0.0, 0.0, 1.0);
    result.cardHit = false;
    result.cardBlendedNormalWS = float3(0.0, 0.0, 1.0);
    result.cardAlbedo = float3(0.0);
    result.cardLighting = float3(0.0);
    result.cardEmissive = float3(0.0);
    result.cardDirectLighting = float3(0.0);
    result.cardIndirectLighting = float3(0.0);
    result.cardWeightSum = 0.0;
    result.cardAxisWeightSum = 0.0;
    result.cardWeightsColor = float3(0.0);
    result.color = float3(1.0, 0.0, 1.0);
    result.hitDistance = maxTraceDistance;

    if (minTraceDistance >= maxTraceDistance)
        return result;

    // 起点先沿射线推到 minTraceDistance——跟 GDF marching 的 minRayTime 是同一个用途(避开
    // 表面自相交), 候选查询和精确求交都从这个推过的起点开始算, 最后把 minTraceDistance 加回
    // 命中距离, 报的是相对调用方原始射线起点的距离
    WorldCoord traceOrigin = rayOriginWS;
    traceOrigin.offset += rayDirectionWS * minTraceDistance;

    uint cascadeIndex = globalDistanceFieldFindStartCascade(cascades, traceOrigin);
    GlobalDistanceFieldCascadeInfo cascade = cascades[cascadeIndex];
    float cellSize = globalDistanceFieldCellSize(cascade);
    float voxelExtent = globalDistanceFieldVoxelExtent(cascade);
    float stepSize = max(globalDistanceFieldObjectGridCellHalfExtent(voxelExtent), 1e-4);
    uint layerOffset = globalDistanceFieldLayerIndirectionOffset(cascadeIndex, true);
    float segmentLength = maxTraceDistance - minTraceDistance;

    uint candidateIndices[kMeshSDFNearFieldMaxCandidates];
    uint numCandidates = 0;

    for (uint step = 0; step <= kMeshSDFNearFieldMaxSteps && numCandidates < kMeshSDFNearFieldMaxCandidates; ++step)
    {
        float t = min(float(step) * stepSize, segmentLength);

        WorldCoord samplePos = traceOrigin;
        samplePos.offset += rayDirectionWS * t;
        float3 posRel = calculateRelativeWorldPosition(samplePos, cascade.center);

        int3 cellCoord = clamp(
            int3(floor((posRel + cascade.halfExtent) / cellSize)),
            int3(0),
            int3(kGlobalDistanceFieldBricksPerAxis - 1));
        uint cellIndex = (uint(cellCoord.z) * uint(kGlobalDistanceFieldBricksPerAxis) + uint(cellCoord.y)) * uint(kGlobalDistanceFieldBricksPerAxis) + uint(cellCoord.x);
        uint slot = gdfIndirection[layerOffset + cellIndex];

        if (slot != kInvalidSparseBrickIndex)
        {
            float3 coordInBrick = (posRel + cascade.halfExtent) / cellSize - float3(cellCoord);
            uint3 objectGridCellCoord = uint3(clamp(
                int3(floor(coordInBrick * float(kGlobalDistanceFieldObjectGridCellsPerBrickAxis))),
                int3(0),
                int3(kGlobalDistanceFieldObjectGridCellsPerBrickAxis - 1)));
            uint entryOffset = globalDistanceFieldObjectGridEntryOffset(slot, globalDistanceFieldObjectGridCellLinearIndex(objectGridCellCoord));

            for (uint k = 0; k < kGlobalDistanceFieldObjectGridCellCapacity; ++k)
            {
                uint entry = objectGrid[entryOffset + k];
                if (entry == kGlobalDistanceFieldObjectGridInvalidEntry)
                    continue;
                uint meshInstanceIndex = unpackGlobalDistanceFieldObjectGridMeshInstanceIndex(entry);

                bool alreadySeen = false;
                for (uint c = 0; c < numCandidates; ++c)
                {
                    if (candidateIndices[c] == meshInstanceIndex)
                    {
                        alreadySeen = true;
                        break;
                    }
                }
                if (!alreadySeen && numCandidates < kMeshSDFNearFieldMaxCandidates)
                {
                    candidateIndices[numCandidates] = meshInstanceIndex;
                    numCandidates++;
                }
            }
        }

        if (t >= segmentLength)
            break;
    }

    bool anyHit = false;
    float closestHitDistance = segmentLength;
    float3 bestNormalOS = float3(0.0, 1.0, 0.0);
    float3 bestPositionOS = float3(0.0);
    MeshInstanceData bestMeshInstance;
    InstanceData bestInstanceData;

    for (uint c = 0; c < numCandidates; ++c)
    {
        MeshInstanceData candidateMeshInstance = meshInstanceData[candidateIndices[c]];
        InstanceData instanceData = sInstanceData[candidateMeshInstance.instanceIndex];

        SparseDistanceFieldObject obj = loadSparseDistanceFieldObject(meshShapeData, candidateMeshInstance.meshShapeDataOffset, mdfAtlasBrickGridExtent);

        WorldCoord objTranslation = instanceData.objectToWorld.getTranslation();
        float3 rayOriginRel = calculateRelativeWorldPosition(traceOrigin, objTranslation);
        float3 rayOriginOS = mul(rayOriginRel, instanceData.worldToObjectRS);

        float3 rayDirOSUnnormalized = mul(rayDirectionWS, instanceData.worldToObjectRS);
        float objectUnitsPerWorldUnit = max(length(rayDirOSUnnormalized), 1e-8);
        float3 rayDirOS = rayDirOSUnnormalized / objectUnitsPerWorldUnit;
        float localMaxDistance = segmentLength * objectUnitsPerWorldUnit;

        SparseDistanceFieldRayHit hit = raymarchSparseDistanceFieldObjectPrimary(
            obj, meshShapeData, mdfBrickAtlas, linearSampler, rayOriginOS, rayDirOS, localMaxDistance);
        if (!hit.hit)
            continue;

        float hitDistanceWS = hit.hitDistance / objectUnitsPerWorldUnit;
        if (hitDistanceWS < closestHitDistance)
        {
            anyHit = true;
            closestHitDistance = hitDistanceWS;
            bestNormalOS = hit.normalOS;
            bestPositionOS = rayOriginOS + rayDirOS * hit.hitDistance;
            bestMeshInstance = candidateMeshInstance;
            bestInstanceData = instanceData;
        }
    }

    if (!anyHit)
    {
        result.valid = false;
        return result;
    }

    result.valid = true;
    result.hitDistance = minTraceDistance + closestHitDistance;
    result.normalWS = normalize(mul(bestNormalOS, bestInstanceData.objectToWorldNormalRot));

    if (bestMeshInstance.cardRangeSlotBase == kGlobalDistanceFieldInvalidCardRangeSlotBase)
        return result; // 精确命中了, 但这个实例还没有可用的 card 数据

    float surfaceCacheBias = kGlobalDistanceFieldObjectGridCardInterpolationRangeInVoxels * voxelExtent;
    uint meshShapeDataOffset = bestMeshInstance.meshShapeDataOffset;
    MeshShapeCardSampleAccumulator acc = sampleMeshShapeSurfaceCache(
        meshShapeData, cardPages,
        cardFinalLightingAtlas, cardAlbedoAtlas, cardNormalAtlas,
        cardEmissiveAtlas, cardDirectLightingAtlas, cardIndirectLightingAtlas, cardDepthAtlas,
        meshShapeDataOffset, loadMeshShapeResolution(meshShapeData, meshShapeDataOffset),
        int(loadMeshShapeCardCount(meshShapeData, meshShapeDataOffset)),
        bestMeshInstance.cardRangeSlotBase, bestPositionOS, bestNormalOS, surfaceCacheBias,
        instanceWorldScale(bestInstanceData));

    result.cardWeightSum = acc.weightSum;
    result.cardAxisWeightSum = acc.axisWeightSum;
    result.cardHit = acc.weightSum >= 1e-4;
    if (result.cardHit)
    {
        float invWeightSum = 1.0 / acc.weightSum;
        result.cardAlbedo = acc.albedoSum * invWeightSum;
        result.cardLighting = acc.lightingSum * invWeightSum;
        result.cardEmissive = acc.emissiveSum * invWeightSum;
        result.cardDirectLighting = acc.directLightingSum * invWeightSum;
        result.cardIndirectLighting = acc.indirectLightingSum * invWeightSum;
        result.cardWeightsColor = acc.debugColorSum * invWeightSum;

        float3 normalSumWS = mul(acc.normalSum, bestInstanceData.objectToWorldNormalRot);
        float blendedLength = length(normalSumWS);
        result.cardBlendedNormalWS = blendedLength > 1e-6 ? normalSumWS / blendedLength : result.normalWS;
    }
    else
    {
        result.color = float3(1.0, 1.0, 0.0);
    }

    return result;
}
