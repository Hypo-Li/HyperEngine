#include "GSplatScene.h"

namespace hyp
{
    GSplatScene::GSplatScene(RenderBackend& renderBackend) :
        mRenderBackend(renderBackend)
    {
        auto& device = renderBackend.getDevice();
        {
            vk::Buffer::CreateInfo bufferInfo;
            bufferInfo.size = sizeof(GPUSplatData) * (1 << 20);
            bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            mSplatDataBuffer = new vk::Buffer(device, bufferInfo, VMA_MEMORY_USAGE_GPU_ONLY);
            mSplatCapacity = static_cast<uint32_t>(bufferInfo.size / sizeof(GPUSplatData));
        }
    }

    GSplatScene::~GSplatScene()
    {
        mRenderBackend.getDeferredTaskQueue().push(
            [splatDataBuffer = std::move(mSplatDataBuffer)]() mutable
            {
                splatDataBuffer = nullptr;
            }
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
        mGSplatInfos[gsplatId] = { UINT32_MAX, count }; // 新增项用offset为UINT32_MAX表示未初始化

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

    void GSplatScene::prepare(vk::CommandBuffer& cmd)
    {
        if (!mPendingOperations.empty())
        {
            flushPendingOperations(cmd);
        }
    }

    void GSplatScene::flushPendingOperations(vk::CommandBuffer& cmd)
    {
        if (mPendingOperations.empty())
            return;

        // 1. 操作预处理, 先遍历PendingOperation队列, 如果一个id先Add后Remove, 则可以将这两个操作都移除
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

        // 原地过滤, 移除可以抵消的操作
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

        // 2. 计算新布局:
        //   a. 遍历gsplatInfos, 剔除需要删除的项目, 计算剩余项(offset != UINT32_MAX且count != 0)在紧实状态下的新offset
        //   b. 计算所有新增项的offset;
        //   c. 计算总容量需求
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

        // 根据oldLayouts合并regions
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

        // 3 根据容量需求判断是否需要扩容,
        // 如果扩容, 则可以直接一步到位, 把每一项复制到对应offset的地方
        // 否则, 需要进行原位拷贝/impact
        if (mSplatCapacity < mTotalSplatCount)
        {
            TRef<vk::Buffer> oldSplatDataBuffer = mSplatDataBuffer;
            mSplatCapacity = std::max(mTotalSplatCount, mSplatCapacity * 2);
            vk::Buffer::CreateInfo bufferInfo;
            bufferInfo.size = mSplatCapacity * sizeof(GPUSplatData);
            bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            mSplatDataBuffer = new vk::Buffer(mRenderBackend.getDevice(), bufferInfo, VMA_MEMORY_USAGE_GPU_ONLY);

            // 拷贝原有数据
            if (!srcGSplatInfos.empty())
            {
                std::vector<VkBufferCopy> copyRegions(srcGSplatInfos.size());
                for (uint32_t i = 0; i < copyRegions.size(); ++i)
                {
                    auto& copy = copyRegions[i];
                    copy.srcOffset = srcGSplatInfos[i].offset * sizeof(GPUSplatData);
                    copy.dstOffset = dstGSplatInfos[i].offset * sizeof(GPUSplatData);
                    copy.size = srcGSplatInfos[i].count * sizeof(GPUSplatData);
                }
                cmd.copyBuffer(
                    oldSplatDataBuffer->getHandle(),
                    mSplatDataBuffer->getHandle(),
                    copyRegions
                );
            }

            // 添加新数据
            if (totalAddedCount != 0)
            {
                auto& stagingAllocator = mRenderBackend.getStagingAllocator();
                auto allocation = stagingAllocator.forceAllocate(totalAddedCount * sizeof(GPUSplatData));
                uint32_t stagingOffset = 0;
                for (const auto& op : mPendingOperations)
                {
                    if (op.type == OpType::Add)
                    {
                        std::memcpy(
                            static_cast<uint8_t*>(allocation.mappedData) + stagingOffset * sizeof(GPUSplatData),
                            op.data.data(),
                            op.data.size() * sizeof(GPUSplatData)
                        );
                        stagingOffset += static_cast<uint32_t>(op.data.size());
                    }
                }
                VkBufferCopy copyRegion = {};
                copyRegion.srcOffset = allocation.offset;
                copyRegion.dstOffset = addedDstOffset * sizeof(GPUSplatData);
                copyRegion.size = totalAddedCount * sizeof(GPUSplatData);
                cmd.copyBuffer(
                    allocation.buffer,
                    mSplatDataBuffer->getHandle(),
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
            // 如果不需要扩容, 则需要进行原位紧实 + 拷贝新数据
            uint32_t aliveTotalCount = addedDstOffset;
            bool alreadyCompact = (srcGSplatInfos.size() == 1 &&
                                    srcGSplatInfos[0].offset == 0 &&
                                    srcGSplatInfos[0].count == aliveTotalCount);
            if (aliveTotalCount && !alreadyCompact)
            {
                // 创建辅助 Buffer
                vk::Buffer::CreateInfo auxBufferInfo;
                auxBufferInfo.size = aliveTotalCount * sizeof(GPUSplatData);
                auxBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                TRef<vk::Buffer> auxBuffer = new vk::Buffer(mRenderBackend.getDevice(), auxBufferInfo, VMA_MEMORY_USAGE_GPU_ONLY);

                // 把存活数据按新 offset 复制到辅助 buffer
                std::vector<VkBufferCopy> copyRegions(srcGSplatInfos.size());
                for (uint32_t i = 0; i < copyRegions.size(); ++i)
                {
                    auto& copy = copyRegions[i];
                    copy.srcOffset = srcGSplatInfos[i].offset * sizeof(GPUSplatData);
                    copy.dstOffset = dstGSplatInfos[i].offset * sizeof(GPUSplatData);
                    copy.size = srcGSplatInfos[i].count * sizeof(GPUSplatData);
                }
                cmd.copyBuffer(
                    mSplatDataBuffer->getHandle(),
                    auxBuffer->getHandle(),
                    copyRegions
                );

                VkBufferMemoryBarrier2 auxBarrier = {};
                auxBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                auxBarrier.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
                auxBarrier.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
                auxBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                auxBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                auxBarrier.buffer = auxBuffer->getHandle();
                auxBarrier.offset = 0;
                auxBarrier.size = VK_WHOLE_SIZE;
                cmd.pipelineBarrier2({ &auxBarrier, 1 });

                // 从辅助 buffer 把紧凑的整块数据 拷贝到原始 buffer 开头
                VkBufferCopy restoreRegion = {};
                restoreRegion.srcOffset = 0;
                restoreRegion.dstOffset = 0;
                restoreRegion.size = aliveTotalCount * sizeof(GPUSplatData);
                cmd.copyBuffer(
                    auxBuffer->getHandle(),
                    mSplatDataBuffer->getHandle(),
                    { &restoreRegion, 1 }
                );

                // 延迟销毁辅助 buffer
                mRenderBackend.getDeferredTaskQueue().push(
                    [auxBuffer]() mutable
                    {
                        auxBuffer = nullptr;
                    },
                    kMaxFramesInFlight
                );
            }

            // 上传新增数据
            if (totalAddedCount != 0)
            {
                auto& stagingAllocator = mRenderBackend.getStagingAllocator();
                auto allocation = stagingAllocator.forceAllocate(totalAddedCount * sizeof(GPUSplatData));
                uint32_t stagingOffset = 0;
                for (const auto& op : mPendingOperations)
                {
                    if (op.type == OpType::Add)
                    {
                        std::memcpy(
                            static_cast<uint8_t*>(allocation.mappedData) + stagingOffset * sizeof(GPUSplatData),
                            op.data.data(),
                            op.data.size() * sizeof(GPUSplatData)
                        );
                        stagingOffset += static_cast<uint32_t>(op.data.size());
                    }
                }
                VkBufferCopy copyRegion = {};
                copyRegion.srcOffset = allocation.offset;
                copyRegion.dstOffset = addedDstOffset * sizeof(GPUSplatData);
                copyRegion.size = totalAddedCount * sizeof(GPUSplatData);
                cmd.copyBuffer(
                    allocation.buffer,
                    mSplatDataBuffer->getHandle(),
                    { &copyRegion, 1 }
                );
            }
        }

        // 最后统一添加barrier
        VkBufferMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = mSplatDataBuffer->getHandle();
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
        cmd.pipelineBarrier2({ &barrier, 1 });

        // 4. 将Remove的id加入freeIds
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
