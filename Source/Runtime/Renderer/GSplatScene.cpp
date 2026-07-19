#include "GSplatScene.h"

namespace hyp
{
    GSplatScene::GSplatScene(RenderBackend& renderBackend) :
        mRenderBackend(renderBackend)
    {
        auto& device = renderBackend.getDevice();
        {
            rhi::BufferDesc bufferDesc;
            bufferDesc.size = sizeof(GPUSplatData) * (1 << 20);
            bufferDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Src | rhi::BufferUsage::Transfer_Dst;
            mSplatDataBuffer = device.createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);
            mSplatCapacity = static_cast<uint32_t>(bufferDesc.size / sizeof(GPUSplatData));
        }
    }

    GSplatScene::~GSplatScene()
    {
        mRenderBackend.getDeferredTaskQueue().push(
            [splatDataBuffer = std::move(mSplatDataBuffer)]() mutable
            {
                splatDataBuffer = nullptr;
            },
            kMaxFramesInFlight
        );
    }

    uint32_t GSplatScene::addGSplat(std::vector<GPUSplatData>&& splatData)
    {
        if (splatData.empty())
            return UINT32_MAX;

        uint32_t gsplatId = UINT32_MAX;
        if (mFreeGSplatIds.empty())
        {
            gsplatId = static_cast<uint32_t>(mGSplatInfos.size());
            mGSplatInfos.resize(gsplatId + 1);
        }
        else
        {
            gsplatId = mFreeGSplatIds.back();
            mFreeGSplatIds.pop_back();
        }
        uint32_t count = static_cast<uint32_t>(splatData.size());
        mGSplatInfos[gsplatId] = { UINT32_MAX, count };

        mPendingOperations.emplace_back(OpType::Add, gsplatId, std::move(splatData));
        mTotalSplatCount += count;

        return gsplatId;
    }

    void GSplatScene::removeGSplat(uint32_t gsplatId)
    {
        if (gsplatId == UINT32_MAX || mGSplatInfos[gsplatId].count == 0)
            return;

        mTotalSplatCount -= mGSplatInfos[gsplatId].count;
        mPendingOperations.emplace_back(OpType::Remove, gsplatId);
        mGSplatInfos[gsplatId].count = 0;
    }

    void GSplatScene::prepare(rhi::CommandList& cmdList)
    {
        if (!mPendingOperations.empty())
        {
            flushPendingOperations(cmdList);
        }
    }

    void GSplatScene::flushPendingOperations(rhi::CommandList& cmdList)
    {
        if (mPendingOperations.empty())
            return;

        std::vector<uint8_t> toRemove(mPendingOperations.size(), 0);
        for (size_t i = 0; i < mPendingOperations.size(); ++i)
        {
            if (toRemove[i]) continue;
            uint32_t id = mPendingOperations[i].gsplatId;
            for (size_t j = i + 1; j < mPendingOperations.size(); ++j)
            {
                if (mPendingOperations[j].gsplatId == id)
                {
                    toRemove[i] = toRemove[j] = 1;
                    mFreeGSplatIds.emplace_back(id);
                    break;
                }
            }
        }

        size_t write = 0;
        for (size_t i = 0; i < mPendingOperations.size(); ++i)
        {
            if (!toRemove[i])
            {
                mPendingOperations[write++] = std::move(mPendingOperations[i]);
            }
        }
        mPendingOperations.resize(write);

        if (mPendingOperations.empty())
            return;

        struct OldLayout
        {
            uint32_t offset;
            uint32_t count;
            uint32_t gsplatId;
        };
        std::vector<OldLayout> oldLayouts;
        oldLayouts.reserve(mGSplatInfos.size());

        for (uint32_t i = 0; i < mGSplatInfos.size(); ++i)
        {
            if (mGSplatInfos[i].count == 0 || mGSplatInfos[i].offset == UINT32_MAX)
                continue;

            oldLayouts.emplace_back(
                mGSplatInfos[i].offset,
                mGSplatInfos[i].count,
                i
            );
        }

        std::sort(oldLayouts.begin(), oldLayouts.end(), [&](auto& a, auto& b)
            {
                return a.offset < b.offset;
            }
        );

        uint32_t currentOffset = 0;
        for (auto& oldLayout : oldLayouts)
        {
            mGSplatInfos[oldLayout.gsplatId].offset = currentOffset;
            currentOffset += oldLayout.count;
        }
        uint32_t addedDstOffset = currentOffset;

        uint32_t totalAddedCount = 0;
        for (const auto& op : mPendingOperations)
        {
            if (op.type == OpType::Add)
            {
                mGSplatInfos[op.gsplatId].offset = currentOffset;
                currentOffset += static_cast<uint32_t>(op.data.size());
                totalAddedCount += static_cast<uint32_t>(op.data.size());
            }
        }

        assert(mTotalSplatCount == currentOffset);

        std::vector<GSplatInfo> srcGSplatInfos;
        std::vector<GSplatInfo> dstGSplatInfos;
        srcGSplatInfos.reserve(oldLayouts.size());
        dstGSplatInfos.reserve(oldLayouts.size());
        for (uint32_t i = 0; i < oldLayouts.size(); ++i)
        {
            if (!srcGSplatInfos.empty() && srcGSplatInfos.back().offset + srcGSplatInfos.back().count == oldLayouts[i].offset)
            {
                srcGSplatInfos.back().count += oldLayouts[i].count;
                dstGSplatInfos.back().count += oldLayouts[i].count;
            }
            else
            {
                srcGSplatInfos.emplace_back(
                    oldLayouts[i].offset,
                    oldLayouts[i].count
                );
                dstGSplatInfos.emplace_back(
                    mGSplatInfos[oldLayouts[i].gsplatId].offset,
                    oldLayouts[i].count
                );
            }
        }

        if (mSplatCapacity < mTotalSplatCount)
        {
            TRef<rhi::Buffer> oldSplatDataBuffer = mSplatDataBuffer;
            mSplatCapacity = std::max(mTotalSplatCount, mSplatCapacity * 2);
            rhi::BufferDesc bufferDesc;
            bufferDesc.size = static_cast<uint64_t>(mSplatCapacity) * sizeof(GPUSplatData);
            bufferDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Src | rhi::BufferUsage::Transfer_Dst;
            mSplatDataBuffer = mRenderBackend.getDevice().createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);

            if (!srcGSplatInfos.empty())
            {
                std::vector<rhi::BufferCopyRegion> copyRegions(srcGSplatInfos.size());
                for (uint32_t i = 0; i < copyRegions.size(); ++i)
                {
                    auto& copy = copyRegions[i];
                    copy.srcOffset = static_cast<uint64_t>(srcGSplatInfos[i].offset) * sizeof(GPUSplatData);
                    copy.dstOffset = static_cast<uint64_t>(dstGSplatInfos[i].offset) * sizeof(GPUSplatData);
                    copy.size = static_cast<uint64_t>(srcGSplatInfos[i].count) * sizeof(GPUSplatData);
                }
                cmdList.copyBuffer(
                    oldSplatDataBuffer.get(),
                    mSplatDataBuffer.get(),
                    copyRegions
                );
            }

            if (totalAddedCount != 0)
            {
                auto& stagingAllocator = mRenderBackend.getStagingAllocator();
                auto allocation = stagingAllocator.forceAllocate(static_cast<uint64_t>(totalAddedCount) * sizeof(GPUSplatData));
                uint32_t stagingOffset = 0;
                for (const auto& op : mPendingOperations)
                {
                    if (op.type == OpType::Add)
                    {
                        std::memcpy(
                            static_cast<uint8_t*>(allocation.mappedData) + static_cast<uint64_t>(stagingOffset) * sizeof(GPUSplatData),
                            op.data.data(),
                            op.data.size() * sizeof(GPUSplatData)
                        );
                        stagingOffset += static_cast<uint32_t>(op.data.size());
                    }
                }
                rhi::BufferCopyRegion copyRegion;
                copyRegion.srcOffset = allocation.offset;
                copyRegion.dstOffset = static_cast<uint64_t>(addedDstOffset) * sizeof(GPUSplatData);
                copyRegion.size = static_cast<uint64_t>(totalAddedCount) * sizeof(GPUSplatData);
                cmdList.copyBuffer(
                    allocation.buffer,
                    mSplatDataBuffer.get(),
                    { &copyRegion, 1 }
                );
            }

            mRenderBackend.getDeferredTaskQueue().push(
                [oldSplatDataBuffer]() mutable
                {
                    oldSplatDataBuffer = nullptr;
                },
                kMaxFramesInFlight
            );
        }
        else
        {
            uint32_t aliveTotalCount = addedDstOffset;
            bool alreadyCompact = (srcGSplatInfos.size() == 1 &&
                                    srcGSplatInfos[0].offset == 0 &&
                                    srcGSplatInfos[0].count == aliveTotalCount);
            if (aliveTotalCount && !alreadyCompact)
            {
                rhi::BufferDesc auxBufferDesc;
                auxBufferDesc.size = static_cast<uint64_t>(aliveTotalCount) * sizeof(GPUSplatData);
                auxBufferDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Src | rhi::BufferUsage::Transfer_Dst;
                TRef<rhi::Buffer> auxBuffer = mRenderBackend.getDevice().createBuffer(auxBufferDesc, rhi::MemoryType::Gpu_Only);

                std::vector<rhi::BufferCopyRegion> copyRegions(srcGSplatInfos.size());
                for (uint32_t i = 0; i < copyRegions.size(); ++i)
                {
                    auto& copy = copyRegions[i];
                    copy.srcOffset = static_cast<uint64_t>(srcGSplatInfos[i].offset) * sizeof(GPUSplatData);
                    copy.dstOffset = static_cast<uint64_t>(dstGSplatInfos[i].offset) * sizeof(GPUSplatData);
                    copy.size = static_cast<uint64_t>(srcGSplatInfos[i].count) * sizeof(GPUSplatData);
                }
                cmdList.copyBuffer(
                    mSplatDataBuffer.get(),
                    auxBuffer.get(),
                    copyRegions
                );

                rhi::BufferBarrier auxBarrier;
                auxBarrier.buffer = auxBuffer.get();
                auxBarrier.offset = 0;
                auxBarrier.size = rhi::kWholeBuffer;
                auxBarrier.oldState = rhi::ResourceState::Transfer_Dst;
                auxBarrier.newState = rhi::ResourceState::Transfer_Src;
                cmdList.insertBarriers({ &auxBarrier, 1 });

                rhi::BufferCopyRegion restoreRegion;
                restoreRegion.srcOffset = 0;
                restoreRegion.dstOffset = 0;
                restoreRegion.size = static_cast<uint64_t>(aliveTotalCount) * sizeof(GPUSplatData);
                cmdList.copyBuffer(
                    auxBuffer.get(),
                    mSplatDataBuffer.get(),
                    { &restoreRegion, 1 }
                );

                mRenderBackend.getDeferredTaskQueue().push(
                    [auxBuffer]() mutable
                    {
                        auxBuffer = nullptr;
                    },
                    kMaxFramesInFlight
                );
            }

            if (totalAddedCount != 0)
            {
                auto& stagingAllocator = mRenderBackend.getStagingAllocator();
                auto allocation = stagingAllocator.forceAllocate(static_cast<uint64_t>(totalAddedCount) * sizeof(GPUSplatData));
                uint32_t stagingOffset = 0;
                for (const auto& op : mPendingOperations)
                {
                    if (op.type == OpType::Add)
                    {
                        std::memcpy(
                            static_cast<uint8_t*>(allocation.mappedData) + static_cast<uint64_t>(stagingOffset) * sizeof(GPUSplatData),
                            op.data.data(),
                            op.data.size() * sizeof(GPUSplatData)
                        );
                        stagingOffset += static_cast<uint32_t>(op.data.size());
                    }
                }
                rhi::BufferCopyRegion copyRegion;
                copyRegion.srcOffset = allocation.offset;
                copyRegion.dstOffset = static_cast<uint64_t>(addedDstOffset) * sizeof(GPUSplatData);
                copyRegion.size = static_cast<uint64_t>(totalAddedCount) * sizeof(GPUSplatData);
                cmdList.copyBuffer(
                    allocation.buffer,
                    mSplatDataBuffer.get(),
                    { &copyRegion, 1 }
                );
            }
        }

        rhi::BufferBarrier barrier;
        barrier.buffer = mSplatDataBuffer.get();
        barrier.offset = 0;
        barrier.size = rhi::kWholeBuffer;
        barrier.oldState = rhi::ResourceState::Transfer_Dst;
        barrier.newState = rhi::ResourceState::Shader_Storage_Read;
        cmdList.insertBarriers({ &barrier, 1 });

        for (const auto& op : mPendingOperations)
        {
            if (op.type == OpType::Remove)
            {
                mFreeGSplatIds.push_back(op.gsplatId);
            }
        }

        mPendingOperations.clear();
    }

}
