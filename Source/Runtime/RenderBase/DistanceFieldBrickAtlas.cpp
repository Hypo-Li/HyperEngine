#include "DistanceFieldBrickAtlas.h"
#include "RenderBackend.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hyp
{
    namespace
    {
        int3 chooseBrickGridExtent(uint32_t capacityInBricks)
        {
            int32_t perAxis = static_cast<int32_t>(std::ceil(std::cbrt(static_cast<double>(std::max(1u, capacityInBricks)))));
            perAxis = std::max(1, perAxis);
            return int3(perAxis, perAxis, perAxis);
        }
    }

    DistanceFieldBrickAtlas::DistanceFieldBrickAtlas(
        RenderBackend& renderBackend,
        uint32_t capacityInBricks,
        bool enableStorageWrite
    ) : mRenderBackend(renderBackend),
        mCapacityInBricks(capacityInBricks),
        mBrickGridExtent(chooseBrickGridExtent(capacityInBricks))
    {
        // 空闲槽位链表初始是 0..capacityInBricks-1 全部空闲; 逆序压入是为了让 allocate()
        // 从尾部 pop 出来的槽位号从小到大, 纯粹是方便调试时肉眼看槽位分布, 不影响正确性
        mFreeSlots.reserve(mCapacityInBricks);
        for (uint32_t i = mCapacityInBricks; i > 0; --i)
            mFreeSlots.push_back(i - 1);

        const int3 atlasExtent = mBrickGridExtent * kDistanceFieldBrickSize;

        rhi::TextureDesc textureDesc;
        textureDesc.type = rhi::TextureType::Texture_3D;
        textureDesc.format = rhi::TextureFormat::R8_UNORM;
        textureDesc.extent = uint3(static_cast<uint32_t>(atlasExtent.x), static_cast<uint32_t>(atlasExtent.y), static_cast<uint32_t>(atlasExtent.z));
        textureDesc.mipCount = 1;
        textureDesc.layerCount = 1;
        textureDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Transfer_Dst;
        if (enableStorageWrite)
            textureDesc.usage |= rhi::TextureUsage::Storage;
        mAtlas = mRenderBackend.getDevice().createTexture(textureDesc, rhi::MemoryType::Gpu_Only);
        mAtlas->setDebugName("DistanceFieldBrickAtlas.Atlas");

        rhi::TextureViewDesc viewDesc;
        viewDesc.type = rhi::TextureViewType::Texture_View_3D;
        viewDesc.format = textureDesc.format;
        viewDesc.range = rhi::TextureRange{ rhi::TextureAspect::Color, 0, 1, 0, 1 };
        mAtlasView = mAtlas->createView(viewDesc);
    }

    DistanceFieldBrickAtlas::~DistanceFieldBrickAtlas()
    {
        mRenderBackend.getDeferredTaskQueue().push(
            [atlasView = std::move(mAtlasView), atlas = std::move(mAtlas)]() mutable
            {
                atlasView = nullptr;
                atlas = nullptr;
            },
            kMaxFramesInFlight
        );
    }

    std::vector<uint32_t> DistanceFieldBrickAtlas::allocate(uint32_t brickCount)
    {
        if (brickCount > mFreeSlots.size())
            return {};

        std::vector<uint32_t> slots(mFreeSlots.end() - brickCount, mFreeSlots.end());
        mFreeSlots.resize(mFreeSlots.size() - brickCount);
        return slots;
    }

    void DistanceFieldBrickAtlas::free(std::span<const uint32_t> slots)
    {
        mFreeSlots.insert(mFreeSlots.end(), slots.begin(), slots.end());
    }

    void DistanceFieldBrickAtlas::queueBrickUploads(std::span<const uint32_t> slots, std::vector<uint8_t>&& voxelData)
    {
        assert(voxelData.size() == slots.size() * kDistanceFieldBrickVoxelCount);

        PendingBricksUpload upload;
        upload.slots.assign(slots.begin(), slots.end());
        upload.voxelData = std::move(voxelData);
        mPendingUploads.push_back(std::move(upload));
    }

    int3 DistanceFieldBrickAtlas::slotToAtlasVoxelOrigin(uint32_t slot) const noexcept
    {
        const int3 gridCoord(
            static_cast<int32_t>(slot % mBrickGridExtent.x),
            static_cast<int32_t>((slot / mBrickGridExtent.x) % mBrickGridExtent.y),
            static_cast<int32_t>(slot / (mBrickGridExtent.x * mBrickGridExtent.y))
        );
        return gridCoord * kDistanceFieldBrickSize;
    }

    void DistanceFieldBrickAtlas::prepare(rhi::CommandList& cmdList)
    {
        if (!mInitialized)
        {
            rhi::TextureBarrier barrier;
            barrier.texture = mAtlas.get();
            barrier.range = mAtlasView->getDesc().range;
            barrier.oldState = rhi::ResourceState::None;
            barrier.newState = rhi::ResourceState::Shader_Sampled;
            cmdList.insertBarriers({}, { &barrier, 1 });

            mInitialized = true;
        }

        if (!mPendingUploads.empty())
        {
            flushUploads(cmdList);
        }
    }

    void DistanceFieldBrickAtlas::flushUploads(rhi::CommandList& cmdList)
    {
        assert(!mPendingUploads.empty());

        // 每个 brick 是 atlas 里彼此不相邻的一小块, 各自的拷贝行距都要单独对齐到 256
        // 字节, 所以每个 brick 在 staging 里占的字节数是"对齐后的行距 * 行数 * 层数",
        // 不是体素数据本身的紧凑大小(8^3 个 uint8 只有 512 字节, 对齐之后每个 brick
        // 实际占用会明显更多, 这是行对齐要求带来的固有开销——R8 格式下这个对齐膨胀比例
        // 比之前的 float32 更高, 因为原始行距缩小了 4 倍, 256 字节边界带来的相对浪费变大)
        const uint32_t unalignedBytesPerRow = static_cast<uint32_t>(kDistanceFieldBrickSize) * static_cast<uint32_t>(sizeof(uint8_t));
        const uint32_t alignedBytesPerRow = (unalignedBytesPerRow + 255u) & ~255u;
        const size_t perBrickStagingSize = static_cast<size_t>(alignedBytesPerRow) * kDistanceFieldBrickSize * kDistanceFieldBrickSize;

        size_t totalBrickCount = 0;
        for (const PendingBricksUpload& upload : mPendingUploads)
            totalBrickCount += upload.slots.size();

        const size_t totalStagingSize = perBrickStagingSize * totalBrickCount;

        auto& stagingAllocator = mRenderBackend.getStagingAllocator();
        std::optional<StagingAllocation> allocation = stagingAllocator.tryAllocate(totalStagingSize);
        if (!allocation)
        {
            // 本帧内可用 staging buffer 已耗尽, 等待下一帧再整批上传——不做"部分上传"是因为
            // 这批 brick 通常是同一个网格烘焙结果的一部分, 拆开跨帧上传除了增加复杂度没有
            // 实际收益(网格的 MeshShapeProxy 反正要等所有 brick 都上传完才算真正可用)
            return;
        }

        std::vector<rhi::BufferTextureCopyRegion> regions;
        regions.reserve(totalBrickCount);

        uint8_t* stagingBase = static_cast<uint8_t*>(allocation->mappedData);
        size_t brickOffset = 0;
        for (const PendingBricksUpload& upload : mPendingUploads)
        {
            for (size_t i = 0; i < upload.slots.size(); ++i)
            {
                uint8_t* dst = stagingBase + brickOffset;
                const uint8_t* src = upload.voxelData.data() + i * kDistanceFieldBrickVoxelCount;
                for (int32_t row = 0; row < kDistanceFieldBrickSize * kDistanceFieldBrickSize; ++row)
                {
                    std::memcpy(dst + static_cast<size_t>(row) * alignedBytesPerRow, src + static_cast<size_t>(row) * kDistanceFieldBrickSize, unalignedBytesPerRow);
                }

                rhi::BufferTextureCopyRegion region;
                region.bufferOffset = allocation->offset + brickOffset;
                region.bytesPerRow = alignedBytesPerRow;
                region.rowsPerTexture = static_cast<uint32_t>(kDistanceFieldBrickSize);
                region.slice = rhi::TextureSlice{ rhi::TextureAspect::Color, 0, 0, 1 };
                region.textureOffset = slotToAtlasVoxelOrigin(upload.slots[i]);
                region.textureExtent = uint3(
                    static_cast<uint32_t>(kDistanceFieldBrickSize),
                    static_cast<uint32_t>(kDistanceFieldBrickSize),
                    static_cast<uint32_t>(kDistanceFieldBrickSize)
                );
                regions.push_back(region);

                brickOffset += perBrickStagingSize;
            }
        }

        rhi::TextureBarrier preBarrier;
        preBarrier.texture = mAtlas.get();
        preBarrier.range = mAtlasView->getDesc().range;
        preBarrier.oldState = rhi::ResourceState::Shader_Sampled;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({}, { &preBarrier, 1 });

        cmdList.copyBufferToTexture(allocation->buffer, mAtlas.get(), regions);

        rhi::TextureBarrier postBarrier;
        postBarrier.texture = mAtlas.get();
        postBarrier.range = mAtlasView->getDesc().range;
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Sampled;
        cmdList.insertBarriers({}, { &postBarrier, 1 });

        mPendingUploads.clear();
    }
}
