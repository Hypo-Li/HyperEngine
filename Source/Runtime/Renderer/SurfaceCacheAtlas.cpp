#include "SurfaceCacheAtlas.h"

#include <RenderBase/RenderBackend.h>

#include <algorithm>
#include <cassert>
#include <cstring>

namespace hyp
{
    namespace
    {
        // 打包格式(shader 端 MeshShapeCard.slang 有镜像): 页号 [0,16) | 宽度档位 [16,19) | 高度档位 [19,22) | 子槽下标 [22,30)
        constexpr uint32_t kPageIndexShift = 0;
        constexpr uint32_t kPageIndexBits = 16;
        constexpr uint32_t kWidthClassShift = kPageIndexShift + kPageIndexBits;
        constexpr uint32_t kSizeClassBits = 3;
        constexpr uint32_t kHeightClassShift = kWidthClassShift + kSizeClassBits;
        constexpr uint32_t kSubSlotShift = kHeightClassShift + kSizeClassBits;
        constexpr uint32_t kSubSlotBits = 8;

        static_assert(kSurfaceCacheAtlasPageCapacity <= (1u << kPageIndexBits),
            "页号位宽 kPageIndexBits 不足以容纳 kSurfaceCacheAtlasPageCapacity 页");

        uint32_t packBits(uint32_t value, uint32_t shift, uint32_t bits) noexcept
        {
            assert(value < (1u << bits) && "打包的字段超出了给它分配的位宽, 检查调用方传的原始值范围");
            return value << shift;
        }

        uint32_t unpackBits(uint32_t packed, uint32_t shift, uint32_t bits) noexcept
        {
            return (packed >> shift) & ((1u << bits) - 1u);
        }

        uint32_t packAddress(const SurfaceCachePageAllocation& allocation) noexcept
        {
            return packBits(allocation.pageIndex, kPageIndexShift, kPageIndexBits)
                | packBits(static_cast<uint32_t>(allocation.widthClass), kWidthClassShift, kSizeClassBits)
                | packBits(static_cast<uint32_t>(allocation.heightClass), kHeightClassShift, kSizeClassBits)
                | packBits(allocation.subSlotIndex, kSubSlotShift, kSubSlotBits);
        }

        SurfaceCachePageAllocation unpackAddress(uint32_t packedAddress) noexcept
        {
            return SurfaceCachePageAllocation{
                unpackBits(packedAddress, kPageIndexShift, kPageIndexBits),
                static_cast<int32_t>(unpackBits(packedAddress, kWidthClassShift, kSizeClassBits)),
                static_cast<int32_t>(unpackBits(packedAddress, kHeightClassShift, kSizeClassBits)),
                unpackBits(packedAddress, kSubSlotShift, kSubSlotBits)
            };
        }
    }

    SurfaceCacheAtlas::SurfaceCacheAtlas(RenderBackend& renderBackend) :
        mRenderBackend(renderBackend),
        mPageAllocator(kSurfaceCacheAtlasResolution),
        mCardPageTableAllocator(kSurfaceCacheAtlasCardPageTableCapacity),
        mCardPageTable(kSurfaceCacheAtlasCardPageTableCapacity, 0u)
    {
        const int2 atlasExtent(kSurfaceCacheAtlasResolution, kSurfaceCacheAtlasResolution);

        rhi::TextureDesc textureDesc;
        textureDesc.type = rhi::TextureType::Texture_2D;
        textureDesc.format = rhi::TextureFormat::RGBA8_UNORM;
        textureDesc.extent = uint3(static_cast<uint32_t>(atlasExtent.x), static_cast<uint32_t>(atlasExtent.y), 1);
        textureDesc.mipCount = 1;
        textureDesc.layerCount = 1;

        rhi::TextureViewDesc viewDesc;
        viewDesc.type = rhi::TextureViewType::Texture_View_2D;
        viewDesc.format = textureDesc.format;
        viewDesc.range = rhi::TextureRange{ rhi::TextureAspect::Color, 0, 1, 0, 1 };

        textureDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage;
        mAlbedoAtlas = mRenderBackend.getDevice().createTexture(textureDesc, rhi::MemoryType::Gpu_Only);
        mAlbedoAtlas->setDebugName("SurfaceCacheAtlas.AlbedoAtlas");
        mAlbedoAtlasView = mAlbedoAtlas->createView(viewDesc);

        mNormalAtlas = mRenderBackend.getDevice().createTexture(textureDesc, rhi::MemoryType::Gpu_Only);
        mNormalAtlas->setDebugName("SurfaceCacheAtlas.NormalAtlas");
        mNormalAtlasView = mNormalAtlas->createView(viewDesc);

        rhi::TextureDesc emissiveTextureDesc = textureDesc;
        emissiveTextureDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
        mEmissiveAtlas = mRenderBackend.getDevice().createTexture(emissiveTextureDesc, rhi::MemoryType::Gpu_Only);
        mEmissiveAtlas->setDebugName("SurfaceCacheAtlas.EmissiveAtlas");

        rhi::TextureViewDesc emissiveViewDesc = viewDesc;
        emissiveViewDesc.format = emissiveTextureDesc.format;
        mEmissiveAtlasView = mEmissiveAtlas->createView(emissiveViewDesc);

        rhi::TextureDesc directLightingTextureDesc = textureDesc;
        directLightingTextureDesc.format = rhi::TextureFormat::B10G11R11_UFLOAT_PACK32;
        // Sampled: 后续被最终画面/GDF 可视化采样; Storage: CardDirectLightingPass 的
        // compute shader 用 RWTexture2D::Store() 直接写, 不经过图形管线
        directLightingTextureDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage;
        mDirectLightingAtlas = mRenderBackend.getDevice().createTexture(directLightingTextureDesc, rhi::MemoryType::Gpu_Only);
        mDirectLightingAtlas->setDebugName("SurfaceCacheAtlas.DirectLightingAtlas");

        rhi::TextureViewDesc directLightingViewDesc = viewDesc;
        directLightingViewDesc.format = directLightingTextureDesc.format;
        mDirectLightingAtlasView = mDirectLightingAtlas->createView(directLightingViewDesc);

        rhi::TextureDesc indirectLightingTextureDesc = textureDesc;
        indirectLightingTextureDesc.format = rhi::TextureFormat::B10G11R11_UFLOAT_PACK32;
        indirectLightingTextureDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage;
        mIndirectLightingAtlas = mRenderBackend.getDevice().createTexture(indirectLightingTextureDesc, rhi::MemoryType::Gpu_Only);
        mIndirectLightingAtlas->setDebugName("SurfaceCacheAtlas.IndirectLightingAtlas");

        rhi::TextureViewDesc indirectLightingViewDesc = viewDesc;
        indirectLightingViewDesc.format = indirectLightingTextureDesc.format;
        mIndirectLightingAtlasView = mIndirectLightingAtlas->createView(indirectLightingViewDesc);

        rhi::TextureDesc finalLightingTextureDesc = textureDesc;
        finalLightingTextureDesc.format = rhi::TextureFormat::B10G11R11_UFLOAT_PACK32;
        finalLightingTextureDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage;
        mFinalLightingAtlas = mRenderBackend.getDevice().createTexture(finalLightingTextureDesc, rhi::MemoryType::Gpu_Only);
        mFinalLightingAtlas->setDebugName("SurfaceCacheAtlas.FinalLightingAtlas");

        rhi::TextureViewDesc finalLightingViewDesc = viewDesc;
        finalLightingViewDesc.format = finalLightingTextureDesc.format;
        mFinalLightingAtlasView = mFinalLightingAtlas->createView(finalLightingViewDesc);

        rhi::TextureDesc numFramesAccumulatedTextureDesc = textureDesc;
        numFramesAccumulatedTextureDesc.format = rhi::TextureFormat::R8_UNORM;
        numFramesAccumulatedTextureDesc.extent = uint3(
            static_cast<uint32_t>(kSurfaceCacheAtlasResolution / kSurfaceCacheCardTileSize),
            static_cast<uint32_t>(kSurfaceCacheAtlasResolution / kSurfaceCacheCardTileSize), 1);
        numFramesAccumulatedTextureDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage;
        mRadiosityNumFramesAccumulatedAtlas = mRenderBackend.getDevice().createTexture(numFramesAccumulatedTextureDesc, rhi::MemoryType::Gpu_Only);
        mRadiosityNumFramesAccumulatedAtlas->setDebugName("SurfaceCacheAtlas.RadiosityNumFramesAccumulatedAtlas");

        rhi::TextureViewDesc numFramesAccumulatedViewDesc = viewDesc;
        numFramesAccumulatedViewDesc.format = numFramesAccumulatedTextureDesc.format;
        mRadiosityNumFramesAccumulatedAtlasView = mRadiosityNumFramesAccumulatedAtlas->createView(numFramesAccumulatedViewDesc);

        rhi::TextureDesc depthTextureDesc;
        depthTextureDesc.type = rhi::TextureType::Texture_2D;
        depthTextureDesc.format = rhi::TextureFormat::R16_UNORM;
        depthTextureDesc.extent = textureDesc.extent;
        depthTextureDesc.mipCount = 1;
        depthTextureDesc.layerCount = 1;
        depthTextureDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage;
        mDepthAtlas = mRenderBackend.getDevice().createTexture(depthTextureDesc, rhi::MemoryType::Gpu_Only);
        mDepthAtlas->setDebugName("SurfaceCacheAtlas.DepthAtlas");

        rhi::TextureViewDesc depthViewDesc = viewDesc;
        depthViewDesc.format = depthTextureDesc.format;
        mDepthAtlasView = mDepthAtlas->createView(depthViewDesc);

        // card page table 是固定大小的场景内共享 buffer, 只在这里建一次——不像页数变化时
        // 需要重建的旧设计(每个实例各自的小 buffer), 这块空间的总大小从一开始就定死了,
        // 变化的只是"这段空间里的哪一段分给了谁"
        rhi::BufferDesc cardPageTableBufferDesc;
        cardPageTableBufferDesc.size = static_cast<uint64_t>(kSurfaceCacheAtlasCardPageTableCapacity) * sizeof(uint32_t);
        cardPageTableBufferDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Dst;
        mCardPageTableBuffer = mRenderBackend.getDevice().createBuffer(cardPageTableBufferDesc, rhi::MemoryType::Gpu_Only);
        mCardPageTableBuffer->setDebugName("SurfaceCacheAtlas.CardPageTable");
    }

    SurfaceCacheAtlas::~SurfaceCacheAtlas()
    {
        // 这个池子现在挂在 LumenScene(每个场景各自懒创建一份)上, 不再是活到引擎关闭才
        // 析构的 RenderBackend 值成员——场景可能在运行中途就被销毁, 这时候 GPU 上可能还有
        // 飞行中的渲染指令在读这两张 atlas 纹理/这块共享 buffer, 延迟到 kMaxFramesInFlight
        // 帧之后再真正销毁, 照抄 GSplatScene::~GSplatScene() 的模式
        mRenderBackend.getDeferredTaskQueue().push(
            [albedoAtlas = std::move(mAlbedoAtlas),
            albedoAtlasView = std::move(mAlbedoAtlasView),
            depthAtlas = std::move(mDepthAtlas),
            depthAtlasView = std::move(mDepthAtlasView),
            normalAtlas = std::move(mNormalAtlas),
            normalAtlasView = std::move(mNormalAtlasView),
            emissiveAtlas = std::move(mEmissiveAtlas),
            emissiveAtlasView = std::move(mEmissiveAtlasView),
            directLightingAtlas = std::move(mDirectLightingAtlas),
            directLightingAtlasView = std::move(mDirectLightingAtlasView),
            finalLightingAtlas = std::move(mFinalLightingAtlas),
            finalLightingAtlasView = std::move(mFinalLightingAtlasView),
            numFramesAccumulatedAtlas = std::move(mRadiosityNumFramesAccumulatedAtlas),
            numFramesAccumulatedAtlasView = std::move(mRadiosityNumFramesAccumulatedAtlasView),
            indirectLightingAtlas = std::move(mIndirectLightingAtlas),
            indirectLightingAtlasView = std::move(mIndirectLightingAtlasView),
            cardPageTableBuffer = std::move(mCardPageTableBuffer)]() mutable
            {
                albedoAtlasView = nullptr;
                albedoAtlas = nullptr;
                depthAtlasView = nullptr;
                depthAtlas = nullptr;
                normalAtlasView = nullptr;
                normalAtlas = nullptr;
                emissiveAtlasView = nullptr;
                emissiveAtlas = nullptr;
                directLightingAtlasView = nullptr;
                directLightingAtlas = nullptr;
                finalLightingAtlasView = nullptr;
                finalLightingAtlas = nullptr;
                numFramesAccumulatedAtlasView = nullptr;
                numFramesAccumulatedAtlas = nullptr;
                indirectLightingAtlasView = nullptr;
                indirectLightingAtlas = nullptr;
                cardPageTableBuffer = nullptr;
            },
            kMaxFramesInFlight
        );
    }

    void SurfaceCacheAtlas::prepare(rhi::CommandList& cmdList)
    {
        if (mInitialized)
            return;
        mInitialized = true;

        TextureClearer& textureClearer = mRenderBackend.getTextureClearer();
        textureClearer.clearStorageTexture(cmdList, *mAlbedoAtlasView, rhi::ResourceState::None, rhi::ResourceState::Shader_Sampled);
        textureClearer.clearStorageTexture(cmdList, *mNormalAtlasView, rhi::ResourceState::None, rhi::ResourceState::Shader_Sampled);
        textureClearer.clearStorageTexture(cmdList, *mEmissiveAtlasView, rhi::ResourceState::None, rhi::ResourceState::Shader_Sampled);
        textureClearer.clearStorageTexture(cmdList, *mDepthAtlasView, rhi::ResourceState::None, rhi::ResourceState::Shader_Sampled);
        textureClearer.clearStorageTexture(cmdList, *mDirectLightingAtlasView, rhi::ResourceState::None, rhi::ResourceState::Shader_Sampled);
        textureClearer.clearStorageTexture(cmdList, *mFinalLightingAtlasView, rhi::ResourceState::None, rhi::ResourceState::Shader_Sampled);
        textureClearer.clearStorageTexture(cmdList, *mRadiosityNumFramesAccumulatedAtlasView, rhi::ResourceState::None, rhi::ResourceState::Shader_Sampled);
        textureClearer.clearStorageTexture(cmdList, *mIndirectLightingAtlasView, rhi::ResourceState::None, rhi::ResourceState::Shader_Sampled);

        GlobalBindings& globalBindings = mRenderBackend.getGlobalBindings();
        cmdList.setBindGroup(
            0,
            rhi::PipelineBindPoint::Compute,
            &globalBindings.getPipelineLayout(),
            &globalBindings.getGlobalDataBindGroup(),
            {}
        );
    }

    std::vector<SurfaceCachePageRegion> SurfaceCacheAtlas::allocateCard(uint32_t rangeSlotBase, uint32_t cardIndex, int32_t resolution)
    {
        CardPageRange range{ 0, int2(0, 0) };
        std::vector<SurfaceCachePageRegion> regions;

        std::vector<SurfaceCachePageAllocation> pages = mPageAllocator.allocate(resolution, resolution);
        if (!pages.empty())
        {
            std::optional<uint32_t> payloadSlotBase = allocateCardPageTableSlots(static_cast<uint32_t>(pages.size()));
            if (payloadSlotBase)
            {
                std::vector<uint32_t> packedAddresses;
                packedAddresses.reserve(pages.size());
                regions.reserve(pages.size());
                for (const SurfaceCachePageAllocation& page : pages)
                {
                    packedAddresses.push_back(packAddress(page));
                    regions.push_back(mPageAllocator.getRegion(page));
                }
                queueCardPageTableUpload(*payloadSlotBase, packedAddresses);

                const int32_t pageCountPerAxis = std::max(1, (resolution + kSurfaceCachePageSize - 1) / kSurfaceCachePageSize);
                assert(pages.size() == static_cast<size_t>(pageCountPerAxis) * static_cast<size_t>(pageCountPerAxis));
                range = CardPageRange{ *payloadSlotBase, int2(pageCountPerAxis, pageCountPerAxis) };
            }
            else
            {
                mPageAllocator.free(pages);
            }
        }

        uint32_t record[3];
        std::memcpy(record, &range, sizeof(range));
        queueCardPageTableUpload(rangeSlotBase + cardIndex * 3u, record);
        return regions;
    }

    SurfaceCachePageRegion SurfaceCacheAtlas::getCardPageRegion(uint32_t rangeSlotBase, uint32_t cardIndex, uint32_t pageLinearIndex) const noexcept
    {
        const CardPageRange range = getCardPageRange(rangeSlotBase, cardIndex);
        assert(pageLinearIndex < static_cast<uint32_t>(range.pageCount.x * range.pageCount.y));
        return mPageAllocator.getRegion(unpackAddress(mCardPageTable[range.pageIndirectionOffset + pageLinearIndex]));
    }

    void SurfaceCacheAtlas::freeCard(uint32_t rangeSlotBase, uint32_t cardIndex) noexcept
    {
        const CardPageRange range = getCardPageRange(rangeSlotBase, cardIndex);
        const uint32_t pageCount = static_cast<uint32_t>(range.pageCount.x) * static_cast<uint32_t>(range.pageCount.y);
        if (pageCount == 0)
            return;

        std::vector<SurfaceCachePageAllocation> pagesToFree;
        pagesToFree.reserve(pageCount);
        for (uint32_t packedAddress : getCardPageTable().subspan(range.pageIndirectionOffset, pageCount))
            pagesToFree.push_back(unpackAddress(packedAddress));
        mPageAllocator.free(pagesToFree);
        freeCardPageTableSlots(range.pageIndirectionOffset, pageCount);
    }

    std::optional<uint32_t> SurfaceCacheAtlas::allocateCardPageTableSlots(uint32_t uint32Count)
    {
        return mCardPageTableAllocator.allocate(uint32Count);
    }

    void SurfaceCacheAtlas::freeCardPageTableSlots(uint32_t offset, uint32_t uint32Count) noexcept
    {
        mCardPageTableAllocator.free(offset, uint32Count);
    }

    void SurfaceCacheAtlas::queueCardPageTableUpload(uint32_t offset, std::span<const uint32_t> data)
    {
        if (data.empty())
            return;

        // 同步写 CPU 常驻拷贝——这一步让当帧的其它消费方(CardCapturePass 等)立刻就能读到
        // 刚写的值, 不用等 flush 真正上传到 GPU, 见 getCardPageTable() 的注释
        assert(offset + data.size() <= mCardPageTable.size() && "写入范围超出了 card page table 的总容量, 上层的 RangeAllocator 分配逻辑出了问题");
        std::memcpy(mCardPageTable.data() + offset, data.data(), data.size() * sizeof(uint32_t));

        mDirtyCardPageTableRanges.push_back(DirtyRange{ offset, offset + static_cast<uint32_t>(data.size()) });
    }

    void SurfaceCacheAtlas::flushCardPageTableUploads(rhi::CommandList& cmdList)
    {
        if (mDirtyCardPageTableRanges.empty())
            return;

        // 合并后的区间互不重叠, 否则同一次 flush 里多条 copy 写同一段字节会构成 WAW 冒险
        std::sort(mDirtyCardPageTableRanges.begin(), mDirtyCardPageTableRanges.end(),
            [](const DirtyRange& a, const DirtyRange& b) { return a.begin < b.begin; });
        std::vector<DirtyRange> mergedRanges;
        for (const DirtyRange& range : mDirtyCardPageTableRanges)
        {
            if (!mergedRanges.empty() && range.begin <= mergedRanges.back().end)
                mergedRanges.back().end = std::max(mergedRanges.back().end, range.end);
            else
                mergedRanges.push_back(range);
        }

        auto& stagingAllocator = mRenderBackend.getStagingAllocator();
        std::vector<StagingAllocation> stagedAllocations;
        stagedAllocations.reserve(mergedRanges.size());
        for (const DirtyRange& range : mergedRanges)
        {
            const size_t byteSize = static_cast<size_t>(range.end - range.begin) * sizeof(uint32_t);
            std::optional<StagingAllocation> allocation = stagingAllocator.tryAllocate(byteSize);
            if (!allocation)
                break; // 本帧内可用 staging buffer 已耗尽, 剩下的区间保持脏, 等待下一帧再上传

            std::memcpy(allocation->mappedData, mCardPageTable.data() + range.begin, byteSize);
            stagedAllocations.push_back(*allocation);
        }

        const size_t stagedCount = stagedAllocations.size();
        mDirtyCardPageTableRanges.assign(mergedRanges.begin() + stagedCount, mergedRanges.end());
        if (stagedCount == 0)
            return;

        // 全场景只有这一个 buffer, 只需要一组 pre/post barrier 覆盖整个 buffer
        rhi::BufferBarrier preBarrier;
        preBarrier.buffer = mCardPageTableBuffer.get();
        preBarrier.offset = 0;
        preBarrier.size = rhi::kWholeBuffer;
        preBarrier.oldState = rhi::ResourceState::Shader_Storage_Read;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({ &preBarrier, 1 });

        for (size_t i = 0; i < stagedCount; ++i)
        {
            const StagingAllocation& allocation = stagedAllocations[i];

            rhi::BufferCopyRegion region;
            region.srcOffset = allocation.offset;
            region.dstOffset = static_cast<uint64_t>(mergedRanges[i].begin) * sizeof(uint32_t);
            region.size = allocation.size;
            cmdList.copyBuffer(allocation.buffer, mCardPageTableBuffer.get(), { &region, 1 });
        }

        rhi::BufferBarrier postBarrier;
        postBarrier.buffer = mCardPageTableBuffer.get();
        postBarrier.offset = 0;
        postBarrier.size = rhi::kWholeBuffer;
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Storage_Read;
        cmdList.insertBarriers({ &postBarrier, 1 });
    }
}
