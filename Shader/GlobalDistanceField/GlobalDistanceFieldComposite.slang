// 把 hyp::GlobalDistanceField::update() 剔选出的候选物体列表合成写进 GDF 自己的 brick
// atlas——一个 thread group 对应一个待合成的 brick(kSparseBrickSize^3 个体素), 循环这个
// brick 的候选物体, 各自变换到候选物体局部空间后调 sampleSparseDistanceField() 采样取
// min, 直接整块覆盖写。不需要单独的 clear pass: 待合成的 brick 每个体素本来就会被这次
// dispatch 完整覆盖一遍, 不依赖 atlas 里的旧内容。
//
// 静态/动态两层不是各自独立、tracer 端再合并——照抄 UE Global Distance Field 的做法:
// 静态层单独合成(只装静态候选), 动态层合成时额外把静态层这同一个格子已经合成好的结果
// 当起点一起取 min, 写成"动态候选 ∪ 静态层结果"的合并值。tracer(第七步)只需要读动态层
// 这一份, 不用每步都采两次样。PC.mergeStaticLayer 控制要不要走这一步(静态层自己合成时
// 不需要, 也没有更早一层可合并)。
//
// 静态 pass、动态 pass 各自把待合成的全部 cascade 一次性拼成一份数据, 上传进全局共享的
// indices buffer(sIndices), 一次 dispatch 处理这一批全部 brick——不再是每个
// cascade/层各自一次 dispatch。布局(见 hyp::kGlobalDistanceFieldCascadeInfoWordCount 等
// 常量的注释, GlobalDistanceField.h):
//   [cascade 信息表: 固定 kGlobalDistanceFieldCascadeCount 条, 按 cascadeIndex 直接索引,
//    每条 8 字: gridMinTile.xyz + gridMinOffset.xyz(asfloat) + cellSize(asfloat) +
//    staticLayerIndirectionOffset(只有动态 pass 有意义)]
//   [合并后的 brick header: 每条 7 字: cascadeSlot, brickIndex, candidateCount,
//    payloadOffset(相对合并后 payload 段的绝对偏移), cellCoord.xyz]
//   [合并后的 payload: 每条 kGlobalDistanceFieldCompositePayloadWordCount 字, 格式见
//    hyp::kGlobalDistanceFieldCompositePayloadWordCount 的注释]
#include "../Common/Binding.slang"
#include "../Common/SparseDistanceField.slang"
#include "GlobalDistanceFieldObjectGrid.slang"

[[vk::binding(0, 3)]]
StructuredBuffer<InstanceData> sInstanceData;

// 全局 indices buffer, 这一批待合成数据整体放在里面, 布局见文件顶部注释
[[vk::binding(1, 3)]]
StructuredBuffer<uint> sIndices;

// 跟 hyp::kGlobalDistanceFieldBricksPerAxis/kGlobalDistanceFieldCascadeCount/
// kGlobalDistanceFieldCascadeInfoWordCount/kGlobalDistanceFieldCascadeInfoTableWordCount/
// kGlobalDistanceFieldCombinedHeaderWordCount/kGlobalDistanceFieldCompositePayloadWordCount/
// kGlobalDistanceFieldInfluenceRangeInVoxels(GlobalDistanceField.h)必须保持一致
static const uint kGlobalDistanceFieldBricksPerAxis = 36;
static const float kGlobalDistanceFieldInfluenceRangeInVoxels = 4.0;
static const uint kGlobalDistanceFieldCascadeCount = 4;
static const uint kGlobalDistanceFieldCascadeInfoWordCount = 8;
static const uint kGlobalDistanceFieldCascadeInfoTableWordCount = kGlobalDistanceFieldCascadeCount * kGlobalDistanceFieldCascadeInfoWordCount;
static const uint kGlobalDistanceFieldCombinedHeaderWordCount = 7;
static const uint kGlobalDistanceFieldCompositePayloadWordCount = 3;

// hyp::RenderBackend::getMeshShapeDataTable() 那张跨网格共享表, metadata + 间接
// 索引表在同一段区间——candidate 用它既查得到 SDF 元数据, 也查得到 brick indirection
[[vk::binding(0, 2)]]
StructuredBuffer<uint> MeshShapeData;

// 候选物体各自的 SDF 源数据, hyp::RenderBackend::getMeshDistanceFieldBrickAtlas() 那张跨网格共享
// brick atlas
[[vk::binding(1, 2)]]
Texture3D<float> SourceBrickAtlas;

[[vk::binding(2, 2)]]
SamplerState LinearSampler;

// 这一级 cascade/层自己的 brick atlas, hyp::LumenScene::getGlobalDistanceFieldBrickAtlas()——
// 声明成 RWTexture3D(不是 WTexture3D), 因为合并静态层结果时要在这同一张纹理里先读一次
// (静态层的 brick, 跟本次要写的动态层 brick 是不同的物理槽位, 读写目标不同, 没有同一
// 体素既读又写的竞争)
[[vk::binding(3, 2)]]
[format("r8")]
RWTexture3D<float> OutputBrickAtlas;

// hyp::GlobalDistanceField::getIndirectionBuffer() 那张 GDF 自己的"cascade 格子 -> atlas
// 槽位号"间接索引表(4 级 cascade × 静态/动态 2 层共用一份 buffer, 各自一段区间)——只有
// PC.mergeStaticLayer 为真时才用得到, 查静态层这同一个 cellIndex 有没有 brick
[[vk::binding(4, 2)]]
StructuredBuffer<uint> GDFIndirection;

// hyp::SceneProxy::getMeshInstanceDataBuffer()——payload 里的 meshInstanceIndex 查这张表
// 拿 instanceIndex/meshShapeDataOffset(结构体定义在 Common/Binding.slang)
[[vk::binding(5, 2)]]
StructuredBuffer<MeshInstanceData> MeshInstanceDataBuffer;

// 动态层合并时会读同一份 buffer 里静态层槽位的内容
[[vk::binding(6, 2)]]
RWStructuredBuffer<uint> ObjectGrid;

// 全部是标量, 不用 float3/int3——float3 后面紧跟标量会被自动垫到 16 字节对齐, 跟 C++ 端
// 手写的逐字段 push constant 结构对不上, 见 MeshSDFPushConstants 的同一条注释。cascade
// 专属的 gridMin/cellSize/staticLayerIndirectionOffset 全部挪进了 sIndices 里的 cascade
// 信息表, 不再逐 dispatch 传
struct GlobalDistanceFieldCompositePushConstants
{
    int sourceAtlasBrickGridExtent;
    int outputAtlasBrickGridExtent;
    int mergeStaticLayer; // 0/1, 见上面 OutputBrickAtlas 的注释
};
[[vk::push_constant]]
GlobalDistanceFieldCompositePushConstants PC;

[shader("compute")]
[numthreads(8, 8, 8)]
void ComputeMain(uint3 groupId : SV_GroupID, uint3 localId : SV_GroupThreadID)
{
    uint headerBase = kGlobalDistanceFieldCascadeInfoTableWordCount + groupId.x * kGlobalDistanceFieldCombinedHeaderWordCount;
    uint cascadeSlot = sIndices[headerBase + 0];
    uint brickIndex = sIndices[headerBase + 1];
    uint candidateCount = sIndices[headerBase + 2];
    uint payloadOffset = sIndices[headerBase + 3];
    int3 cellCoord = int3(sIndices[headerBase + 4], sIndices[headerBase + 5], sIndices[headerBase + 6]);

    uint infoBase = cascadeSlot * kGlobalDistanceFieldCascadeInfoWordCount;
    WorldCoord gridMinCoord;
    gridMinCoord.tile = int3(sIndices[infoBase + 0], sIndices[infoBase + 1], sIndices[infoBase + 2]);
    gridMinCoord.offset = asfloat(uint3(sIndices[infoBase + 3], sIndices[infoBase + 4], sIndices[infoBase + 5]));
    float cellSize = asfloat(sIndices[infoBase + 6]);
    uint staticLayerIndirectionOffset = sIndices[infoBase + 7];

    // cascade 里每个格子(cell)固定切成 kSparseUniqueBrickSize 份"唯一"体素, 第 kSparseBrickSize
    // 份(localId 的最后一格)是跟相邻格子共享的重叠边界, 换算方式必须跟 sparseDistanceFieldResolveBrick()
    // 里 brick->voxel 的换算互为逆运算
    float voxelWorldSize = cellSize / float(kSparseUniqueBrickSize);
    // cascade 总跨度远小于 WORLD_TILE_SIZE(1024), 这一段小偏移可以直接用 float 叠加, 不需要
    // 再单独换算 tile
    float3 voxelOffsetFromGridMin = float3(cellCoord) * cellSize + float3(localId) * voxelWorldSize;

    // C++ 端剔选候选用的半径就是 influenceRange(GlobalDistanceField.cpp 的 cellPadding),
    // 超出它的距离值没有考虑这个范围以外的物体, 拿去给球追踪当步长会直接跨过别的表面
    float influenceRange = kGlobalDistanceFieldInfluenceRangeInVoxels * voxelWorldSize;
    float minDist = influenceRange;

    // 偶数 localId 的体素中心就是 Object Grid 格子的采样中心
    bool writesObjectGridCell = all((localId % 2u) == 0u);
    uint objectGridCellLinearIndex = globalDistanceFieldObjectGridCellLinearIndex(localId / 2u);
    GlobalDistanceFieldObjectGridCell objectGridCell = initGlobalDistanceFieldObjectGridCell();
    float voxelExtent = 0.5 * voxelWorldSize;

    // 合并静态层: 这个格子如果静态层也有 brick, 先读它已经合成好的体素值当起点——两边
    // 用的是同一套"cellCoord -> cell 的世界格子坐标"编码, 直接换算 cellIndex 去查
    // GDFIndirection 就行, 不需要重新做候选剔选
    if (PC.mergeStaticLayer != 0)
    {
        uint cellIndex = (uint(cellCoord.z) * kGlobalDistanceFieldBricksPerAxis + uint(cellCoord.y)) * kGlobalDistanceFieldBricksPerAxis + uint(cellCoord.x);
        uint staticSlot = GDFIndirection[staticLayerIndirectionOffset + cellIndex];
        if (staticSlot != kInvalidSparseBrickIndex)
        {
            int3 staticAtlasOrigin = sparseBrickSlotToAtlasOrigin(staticSlot, PC.outputAtlasBrickGridExtent);
            minDist = decodeSignedDistance(OutputBrickAtlas[staticAtlasOrigin + int3(localId)], influenceRange);

            if (writesObjectGridCell)
            {
                uint staticEntryOffset = globalDistanceFieldObjectGridEntryOffset(staticSlot, objectGridCellLinearIndex);
                for (uint k = 0; k < kGlobalDistanceFieldObjectGridCellCapacity; ++k)
                    objectGridCell.entries[k] = ObjectGrid[staticEntryOffset + k];
            }
        }
    }

    for (uint i = 0; i < candidateCount; ++i)
    {
        uint payloadBase = payloadOffset + i * kGlobalDistanceFieldCompositePayloadWordCount;
        uint meshInstanceIndex = sIndices[payloadBase];
        MeshInstanceData meshInstanceData = MeshInstanceDataBuffer[meshInstanceIndex];
        uint instanceIndex = meshInstanceData.instanceIndex;
        uint meshShapeDataOffset = meshInstanceData.meshShapeDataOffset;

        SparseDistanceFieldObject obj = loadSparseDistanceFieldObject(MeshShapeData, meshShapeDataOffset, PC.sourceAtlasBrickGridExtent);

        InstanceData instanceData = sInstanceData[instanceIndex];
        WorldCoord objTranslation = instanceData.objectToWorld.getTranslation();
        // gridMin 相对这个候选物体所在 tile 的小数偏移, 再叠加体素在 cascade 内部的小偏移,
        // 得到的正是 worldToObjectRS 期望的"世界轴对齐、相对物体原点"的向量
        float3 relativePos = calculateRelativeWorldPosition(gridMinCoord, objTranslation) + voxelOffsetFromGridMin;
        float3 positionOS = mul(relativePos, instanceData.worldToObjectRS);

        // 照抄 UE MeshDistanceFieldCommon.ush::DistanceToNearestSurfaceForObject() 的分段
        // 换算, 不能对"包围盒内窄带纹理值"和"包围盒外的解析距离"套同一个折算比例:
        // - 包围盒外的部分在局部空间就已经知道是沿哪根轴超出的, 逐轴乘各自真实的世界缩放
        //   (worldScale, 未折叠)再合成欧氏距离, 对非均匀缩放是精确的
        // - 包围盒内纹理采样出来的窄带值不知道朝哪个方向, 没法精确换算, 只能保守地乘三轴
        //   缩放里最小的一个(min(scale)): 局部空间半径 d 的"保证无表面"球经非均匀缩放后
        //   是个世界空间椭球, 能内接的最大世界球半径是 d*min(scale), 用更大的系数会在压缩
        //   最少的那根轴方向上高估距离, 球追踪步进可能直接穿过表面
        float3x3 rs = instanceData.objectToWorldRS;
        float3 worldScale = float3(length(rs[0]), length(rs[1]), length(rs[2]));
        float volumeScale = min(worldScale.x, min(worldScale.y, worldScale.z));

        float3 boundsCenterOS = 0.5 * (obj.boundsMin + obj.boundsMax);
        float3 innerHalfExtentOS = sparseDistanceFieldHalfExtentWithoutBorder(obj);
        float3 clampedPositionOS = clamp(positionOS, boundsCenterOS - innerHalfExtentOS, boundsCenterOS + innerHalfExtentOS);
        float3 outsideDeltaOS = abs(positionOS - boundsCenterOS) - innerHalfExtentOS;
        float3 toBox = outsideDeltaOS * worldScale;
        float distanceToBoxOutside = length(max(toBox, float3(0.0)));
        float distanceToBoxInside = min(0.0, max(toBox.x, max(toBox.y, toBox.z)));
        float distanceToBox = distanceToBoxOutside + distanceToBoxInside;

        float innerDist = sampleSparseDistanceFieldClamped(obj, MeshShapeData, SourceBrickAtlas, LinearSampler, clampedPositionOS) * volumeScale;
        // 不让包围盒内窄带值算出的负距离(在物体内部)探出比 distanceToBox 更远的地方
        float dist = max(innerDist + max(0.0, distanceToBox), distanceToBox);
        minDist = min(minDist, dist);

        if (writesObjectGridCell)
        {
            uint maskWord = sIndices[payloadBase + 1u + objectGridCellLinearIndex / 32u];
            if ((maskWord & (1u << (objectGridCellLinearIndex % 32u))) != 0u)
                addToGlobalDistanceFieldObjectGridCell(objectGridCell, meshInstanceIndex, dist, voxelExtent);
        }
    }

    int3 outputAtlasOrigin = sparseBrickSlotToAtlasOrigin(brickIndex, PC.outputAtlasBrickGridExtent);
    OutputBrickAtlas.Store(outputAtlasOrigin + int3(localId), encodeSignedDistance(minDist, influenceRange));

    if (writesObjectGridCell)
    {
        sortGlobalDistanceFieldObjectGridCell(objectGridCell);
        uint entryOffset = globalDistanceFieldObjectGridEntryOffset(brickIndex, objectGridCellLinearIndex);
        for (uint k = 0; k < kGlobalDistanceFieldObjectGridCellCapacity; ++k)
            ObjectGrid[entryOffset + k] = objectGridCell.entries[k];
    }
}
