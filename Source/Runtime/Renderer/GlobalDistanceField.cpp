#include "GlobalDistanceField.h"

#include "MeshRenderProxy.h"

#include <RenderBase/MeshShapeProxy.h>
#include <RenderBase/RenderBackend.h>
#include <RenderBase/DistanceFieldBrickAtlas.h>
#include <RenderBase/StagingAllocator.h>

#include <Core/Math/Box.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hyp
{
    GlobalDistanceField::GlobalDistanceField(RenderBackend& renderBackend) noexcept :
        mRenderBackend(renderBackend)
    {
        for (int32_t i = 0; i < kGlobalDistanceFieldCascadeCount; ++i)
            mCascades[i].halfExtent = kGlobalDistanceFieldInnermostHalfExtent * static_cast<float>(1 << i);
    }

    GlobalDistanceField::~GlobalDistanceField() noexcept
    {
        mRenderBackend.getDeferredTaskQueue().push(
            [indirectionBuffer = std::move(mIndirectionBuffer),
            objectGridBuffer = std::move(mObjectGridBuffer)]() mutable
            {
                indirectionBuffer = nullptr;
                objectGridBuffer = nullptr;
            },
            kMaxFramesInFlight
        );
    }

    void GlobalDistanceField::update(std::span<MeshRenderProxy* const> meshRenderProxies, DistanceFieldBrickAtlas& brickAtlas)
    {
        const int3 atlasGridExtent = brickAtlas.getBrickGridExtent();
        ensureObjectGridBuffer(static_cast<uint32_t>(atlasGridExtent.x) * atlasGridExtent.y * atlasGridExtent.z);

        for (int32_t cascadeIndex = 0; cascadeIndex < kGlobalDistanceFieldCascadeCount; ++cascadeIndex)
        {
            Cascade& cascade = mCascades[cascadeIndex];
            updateCascadeLayer(meshRenderProxies, brickAtlas, cascade, cascade.staticLayer, Mobility::Static, getLayerIndirectionOffset(cascadeIndex, false), false);
            updateCascadeLayer(meshRenderProxies, brickAtlas, cascade, cascade.dynamicLayer, Mobility::Dynamic, getLayerIndirectionOffset(cascadeIndex, true), true);
        }
    }

    void GlobalDistanceField::prepare(rhi::CommandList& cmdList)
    {
        if (!mPendingIndirectionUploads.empty())
        {
            // barrier 覆盖整个 buffer(不是只覆盖这一批待上传的那几段区间)——跟 SurfaceCacheAtlas
            // 页表那套一样, 图省事: 8 层里只要有一层曾经上传过, 就认为整个 buffer 已经离开
            // UNDEFINED, 其余还没被任何一层碰过的区间内容是垃圾, 但那些区间对应的 cascade
            // 层 dirty 仍然是 true, 不会被采样端读到
            rhi::BufferBarrier preBarrier;
            preBarrier.buffer = mIndirectionBuffer.get();
            preBarrier.offset = 0;
            preBarrier.size = rhi::kWholeBuffer;
            preBarrier.oldState = mIndirectionBufferUploaded ? rhi::ResourceState::Shader_Storage_Read : rhi::ResourceState::None;
            preBarrier.newState = rhi::ResourceState::Transfer_Dst;
            cmdList.insertBarriers({ &preBarrier, 1 });

            for (const PendingIndirectionUpload& pending : mPendingIndirectionUploads)
            {
                rhi::BufferCopyRegion region;
                region.srcOffset = pending.staging.offset;
                region.dstOffset = static_cast<uint64_t>(pending.offset) * sizeof(uint32_t);
                region.size = static_cast<uint64_t>(pending.wordCount) * sizeof(uint32_t);
                cmdList.copyBuffer(pending.staging.buffer, mIndirectionBuffer.get(), { &region, 1 });
            }

            rhi::BufferBarrier postBarrier;
            postBarrier.buffer = mIndirectionBuffer.get();
            postBarrier.offset = 0;
            postBarrier.size = rhi::kWholeBuffer;
            postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
            postBarrier.newState = rhi::ResourceState::Shader_Storage_Read;
            cmdList.insertBarriers({ &postBarrier, 1 });

            mIndirectionBufferUploaded = true;
            mPendingIndirectionUploads.clear();
        }
    }

    void GlobalDistanceField::updateCascadeLayer(
        std::span<MeshRenderProxy* const> meshRenderProxies,
        DistanceFieldBrickAtlas& brickAtlas,
        const Cascade& cascade,
        Layer& layer,
        Mobility mobility,
        uint32_t indirectionOffset,
        bool mergeStaticLayer
    )
    {
        if (!layer.dirty)
            return;

        const int32_t bricksPerAxis = kGlobalDistanceFieldBricksPerAxis;
        const double cellSize = static_cast<double>(2.0f * cascade.halfExtent) / bricksPerAxis;
        const double3 gridMin = cascade.center - double3(cascade.halfExtent);
        const Boxd cascadeBounds(gridMin, cascade.center + double3(cascade.halfExtent));
        const double cellPadding = kGlobalDistanceFieldInfluenceRangeInVoxels * (cellSize / kDistanceFieldUniqueBrickSize);

        // 按 mobility 过滤 + 跟这一级 cascade 的世界范围有 AABB 相交的候选物体——场景
        // 规模不大时暴力遍历足够, 不需要额外的空间加速结构。没有 MeshShapeProxy 或者它
        // 没有距离场数据(没跑过 ensureDistanceFieldData, 或者烘焙/分配失败)的实例直接跳过,
        // 合成阶段没有它们的距离场数据可采样
        std::vector<MeshRenderProxy*> candidates;
        for (MeshRenderProxy* meshRenderProxy : meshRenderProxies)
        {
            if (meshRenderProxy->getMobility() != mobility)
                continue;
            MeshShapeProxy* meshShapeProxy = meshRenderProxy->getMeshShapeProxy();
            if (!meshShapeProxy || !meshShapeProxy->hasDistanceFieldData())
                continue;
            if (cascadeBounds.intersect(meshRenderProxy->getBoundingBox()))
                candidates.push_back(meshRenderProxy);
        }

        std::vector<uint32_t> newIndirection(static_cast<size_t>(bricksPerAxis) * bricksPerAxis * bricksPerAxis, kInvalidDistanceFieldBrickIndex);
        std::vector<uint32_t> newlyAllocatedSlots;

        // 边剔选边直接拍平成 layer.pendingHeaderWords/pendingPayloadWords——header 里的
        // payloadOffset 先记"相对 payload 段自己的偏移", 等这层最终 header 总字数确定后,
        // 在下面统一加上(payload 段紧跟在 header 段后面)
        std::vector<uint32_t> headerWords;
        std::vector<uint32_t> payloadWords;

        const double voxelSize = cellSize / kDistanceFieldUniqueBrickSize;
        const double objectGridCellHalfExtent = cellSize / (2.0 * kGlobalDistanceFieldObjectGridCellsPerBrickAxis);
        const double cardInterpolationRange = kGlobalDistanceFieldObjectGridCardInterpolationRangeInVoxels * 0.5 * voxelSize;

        // 候选物体自己的 MDF 假设在这一帧内已经烘焙上传完毕, 不检查是否还在 pending——
        // 场景规模变大、懒加载的 per-mesh SDF 跟不上合成节奏时需要重新处理这个问题。
        std::vector<uint32_t> cellsToVisit;
        if (mergeStaticLayer)
            cellsToVisit = cascade.staticLayer.occupiedCells;
        for (MeshRenderProxy* candidate : candidates)
        {
            const auto toCellCoord = [&](double offsetFromGridMin)
                {
                    return static_cast<int32_t>(std::clamp(std::floor(offsetFromGridMin / cellSize), -2.0, static_cast<double>(bricksPerAxis) + 1.0));
                };
            const Boxd& candidateBounds = candidate->getBoundingBox();
            const double3 paddedMin = candidateBounds.getMin() - double3(cellPadding) - gridMin;
            const double3 paddedMax = candidateBounds.getMax() + double3(cellPadding) - gridMin;

            // 每边多放一格抵消取整的浮点误差, 范围内的格子仍由下面的 paddedCellBounds.intersect() 精确判定
            const int32_t minX = std::max(toCellCoord(paddedMin.x) - 1, 0);
            const int32_t minY = std::max(toCellCoord(paddedMin.y) - 1, 0);
            const int32_t minZ = std::max(toCellCoord(paddedMin.z) - 1, 0);
            const int32_t maxX = std::min(toCellCoord(paddedMax.x) + 1, bricksPerAxis - 1);
            const int32_t maxY = std::min(toCellCoord(paddedMax.y) + 1, bricksPerAxis - 1);
            const int32_t maxZ = std::min(toCellCoord(paddedMax.z) + 1, bricksPerAxis - 1);
            for (int32_t z = minZ; z <= maxZ; ++z)
            {
                for (int32_t y = minY; y <= maxY; ++y)
                {
                    for (int32_t x = minX; x <= maxX; ++x)
                        cellsToVisit.push_back((static_cast<uint32_t>(z) * bricksPerAxis + y) * bricksPerAxis + x);
                }
            }
        }
        std::sort(cellsToVisit.begin(), cellsToVisit.end());
        cellsToVisit.erase(std::unique(cellsToVisit.begin(), cellsToVisit.end()), cellsToVisit.end());

        std::vector<uint32_t> newOccupiedCells;
        const std::vector<uint32_t>& staticIndirection = cascade.staticLayer.indirectionCPU;
        std::vector<MeshRenderProxy*> cellCandidates;
        for (uint32_t cellIndex : cellsToVisit)
        {
            const int32_t x = static_cast<int32_t>(cellIndex % bricksPerAxis);
            const int32_t y = static_cast<int32_t>((cellIndex / bricksPerAxis) % bricksPerAxis);
            const int32_t z = static_cast<int32_t>(cellIndex / (bricksPerAxis * bricksPerAxis));
            const double3 cellMin = gridMin + double3(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)) * cellSize;
            const Boxd cellBounds(cellMin, cellMin + double3(cellSize));
            const Boxd paddedCellBounds(cellBounds.getMin() - double3(cellPadding), cellBounds.getMax() + double3(cellPadding));

            cellCandidates.clear();
            for (MeshRenderProxy* candidate : candidates)
            {
                if (paddedCellBounds.intersect(candidate->getBoundingBox()))
                    cellCandidates.push_back(candidate);
            }

            const bool staticHasBrick = mergeStaticLayer
                && cellIndex < staticIndirection.size()
                && staticIndirection[cellIndex] != kInvalidDistanceFieldBrickIndex;
            if (cellCandidates.empty() && !staticHasBrick)
                continue;

            std::vector<uint32_t> allocated = brickAtlas.allocate(1);
            if (allocated.empty())
                continue; // 池子满了, 这个格子这一轮没有 brick, 下次这一层再被标脏时重试

            newIndirection[cellIndex] = allocated[0];
            newlyAllocatedSlots.push_back(allocated[0]);
            newOccupiedCells.push_back(cellIndex);

            const uint32_t payloadOffset = static_cast<uint32_t>(payloadWords.size());
            for (MeshRenderProxy* candidate : cellCandidates)
            {
                const Boxd& candidateBounds = candidate->getBoundingBox();
                const double3 boundsCenter = candidateBounds.getCenter();
                const double3 boundsHalfExtent = (candidateBounds.getMax() - candidateBounds.getMin()) * 0.5;

                uint64_t objectGridCellMask = 0;
                for (uint32_t gz = 0; gz < kGlobalDistanceFieldObjectGridCellsPerBrickAxis; ++gz)
                {
                    for (uint32_t gy = 0; gy < kGlobalDistanceFieldObjectGridCellsPerBrickAxis; ++gy)
                    {
                        for (uint32_t gx = 0; gx < kGlobalDistanceFieldObjectGridCellsPerBrickAxis; ++gx)
                        {
                            // 掩码表示"哪些查询会落进这一格", 格子范围必须跟 GlobalDistanceFieldQuery.slang
                            // 里 floor(coordInBrick * kGlobalDistanceFieldObjectGridCellsPerBrickAxis) 的等分划分一致
                            const double objectGridCellSize = cellSize / double(kGlobalDistanceFieldObjectGridCellsPerBrickAxis);
                            const double3 objectGridCellCenter =
                                cellMin + (double3(gx, gy, gz) + 0.5) * objectGridCellSize;
                            const double3 separation = glm::max(
                                glm::abs(objectGridCellCenter - boundsCenter) - (boundsHalfExtent + double3(objectGridCellHalfExtent)),
                                double3(0.0));
                            if (glm::dot(separation, separation) <= cardInterpolationRange * cardInterpolationRange)
                            {
                                const uint32_t linearIndex = (gz * kGlobalDistanceFieldObjectGridCellsPerBrickAxis + gy) * kGlobalDistanceFieldObjectGridCellsPerBrickAxis + gx;
                                objectGridCellMask |= uint64_t(1) << linearIndex;
                            }
                        }
                    }
                }

                payloadWords.push_back(candidate->getMeshInstanceIndex());
                payloadWords.push_back(static_cast<uint32_t>(objectGridCellMask));
                payloadWords.push_back(static_cast<uint32_t>(objectGridCellMask >> 32));
            }

            headerWords.push_back(allocated[0]);
            headerWords.push_back(static_cast<uint32_t>(cellCandidates.size()));
            headerWords.push_back(payloadOffset);
            headerWords.push_back(static_cast<uint32_t>(x));
            headerWords.push_back(static_cast<uint32_t>(y));
            headerWords.push_back(static_cast<uint32_t>(z));
        }

        if (!queueLayerIndirectionUpload(indirectionOffset, newIndirection))
        {
            // staging 预算不够, 这次尝试作废: 回滚刚分配的新槽位, 旧的 indirectionCPU/GPU
            // buffer 内容原样保留, layer.dirty 保持 true, 下一帧整个重来
            brickAtlas.free(newlyAllocatedSlots);
            return;
        }

        // 走到这里, 间接索引表已经真的写进 GPU buffer 了, 没法反悔——不管接下来合成 buffer
        // 上传成不成功, 这次新分配的槽位/新的间接索引表内容都要照常提交, 旧槽位照常释放。
        // 上传成功后才能放心释放旧槽位——新的先分配、旧的后释放, 万一中途分配池子满了
        // 或者上传失败, 旧内容始终完整可用, 不会出现新旧都拿不到 brick 的窗口期
        std::vector<uint32_t> oldSlotsToFree;
        oldSlotsToFree.reserve(layer.occupiedCells.size());
        for (uint32_t cellIndex : layer.occupiedCells)
            oldSlotsToFree.push_back(layer.indirectionCPU[cellIndex]);
        if (!oldSlotsToFree.empty())
            brickAtlas.free(oldSlotsToFree);

        layer.indirectionCPU = std::move(newIndirection);
        layer.occupiedCells = std::move(newOccupiedCells);
        layer.dirty = false;

        // headerWords 里的 payloadOffset 就是最终要交出去的值: 相对这一层自己的
        // pendingPayloadWords 起始位置(push 的时候已经是这个语义, 不需要再回填)——
        // GlobalDistanceFieldCompositePass 会在合并多个层的时候, 把它加上"这一层的 payload
        // 段在合并后 payload 里的起始偏移", 换算成最终绝对偏移, 不是这里的职责

        // 不在这里上传 GPU buffer——真正的拼装/上传/dispatch 留给 GlobalDistanceFieldCompositePass
        // 在自己的 pass 里统一做(多个层的数据合并成一份、一次 dispatch, 见该 pass 的注释)。
        // 这里只需要把 CPU 端算好的结果交出去
        layer.pendingHeaderWords = std::move(headerWords);
        layer.pendingPayloadWords = std::move(payloadWords);
        layer.needsComposite = !layer.pendingHeaderWords.empty();
    }

    bool GlobalDistanceField::queueLayerIndirectionUpload(uint32_t offset, std::span<const uint32_t> data)
    {
        if (!mIndirectionBuffer)
        {
            // 一份 buffer 覆盖全部 cascade × 层(4 级 × 静态/动态 2 层), 每层固定大小
            // kGlobalDistanceFieldCellsPerCascade, 懒创建——没开 Lumen 的相机永远不会
            // 走到这里
            rhi::BufferDesc desc;
            desc.size = static_cast<uint64_t>(kGlobalDistanceFieldCascadeCount) * 2 * kGlobalDistanceFieldCellsPerCascade * sizeof(uint32_t);
            desc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Dst;
            mIndirectionBuffer = mRenderBackend.getDevice().createBuffer(desc, rhi::MemoryType::Gpu_Only);
            mIndirectionBuffer->setDebugName("GlobalDistanceField.Indirection");
        }

        auto& stagingAllocator = mRenderBackend.getStagingAllocator();
        const size_t byteSize = data.size() * sizeof(uint32_t);
        std::optional<StagingAllocation> allocation = stagingAllocator.tryAllocate(byteSize);
        if (!allocation)
            return false;

        std::memcpy(allocation->mappedData, data.data(), byteSize);
        mPendingIndirectionUploads.push_back({ offset, *allocation, static_cast<uint32_t>(data.size()) });
        return true;
    }

    void GlobalDistanceField::ensureObjectGridBuffer(uint32_t atlasSlotCapacity)
    {
        if (mObjectGridBuffer)
            return;

        rhi::BufferDesc desc;
        desc.size = static_cast<uint64_t>(atlasSlotCapacity) * kGlobalDistanceFieldObjectGridWordsPerBrick * sizeof(uint32_t);
        desc.usage = rhi::BufferUsage::Storage;
        mObjectGridBuffer = mRenderBackend.getDevice().createBuffer(desc, rhi::MemoryType::Gpu_Only);
        mObjectGridBuffer->setDebugName("GlobalDistanceField.ObjectGrid");
    }
}
