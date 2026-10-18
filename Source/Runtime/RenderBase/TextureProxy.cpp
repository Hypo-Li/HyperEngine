#include "TextureProxy.h"
#include "RenderBackend.h"

#include <algorithm>

namespace hyp
{
    TextureProxy::TextureProxy(const rhi::TextureDesc& desc)
    {
        auto& renderBackend = getRenderBackend();
        auto& device = renderBackend.getDevice();
        mTexture = device.createTexture(desc, rhi::MemoryType::Gpu_Only);
        rhi::TextureViewDesc viewDesc = rhi::makeTextureDefaultViewDesc(desc);
        mTextureView = mTexture->createView(viewDesc);
        mInitialized = false;

        if (device.getFeatures().bindless)
        {
            auto& bindlessRegistry = renderBackend.getBindlessRegistry();
            if (viewDesc.type == rhi::TextureViewType::Texture_View_2D)
                mBindlessHandle = bindlessRegistry.registerTexture2D(mTextureView);
            else if (viewDesc.type == rhi::TextureViewType::Texture_View_2D_Array)
                mBindlessHandle = bindlessRegistry.registerTexture2DArray(mTextureView);
            else if (viewDesc.type == rhi::TextureViewType::Texture_View_3D)
                mBindlessHandle = bindlessRegistry.registerTexture3D(mTextureView);
            else if (viewDesc.type == rhi::TextureViewType::Texture_View_Cube)
                mBindlessHandle = bindlessRegistry.registerTextureCube(mTextureView);
        }
    }

    TextureProxy::~TextureProxy() noexcept
    {
        auto& renderBackend = getRenderBackend();
        bool bindless = renderBackend.getDevice().getFeatures().bindless;
        renderBackend.getDeferredTaskQueue().push(
            [texture = std::move(mTexture),
            textureView = std::move(mTextureView),
            bindlessRegistry = bindless ? &renderBackend.getBindlessRegistry() : nullptr,
            bindlessHandle = std::move(mBindlessHandle)]() mutable
            {
                if (bindlessRegistry && bindlessHandle.isValid())
                    bindlessRegistry->unregister(bindlessHandle);
                textureView = nullptr;
                texture = nullptr;
            },
            kMaxFramesInFlight
        );
    }

    void TextureProxy::stageUpload(std::vector<uint8_t>&& data, uint32_t mipLevel, uint32_t baseLayer, uint32_t layerCount)
    {
        // flushUploads() 把 mPendingUploads 里所有条目的 copyBufferToTexture 都塞在同一对
        // pre/post barrier 之间, 中间没有互相同步——如果两条待上传条目命中同一个 subresource,
        // 会被驱动的同步校验判定为 WRITE_AFTER_WRITE hazard(两次拷贝之间没有任何依赖关系)。
        // 同一个 slice 在还没被 flush 之前又被重新 stageUpload 是可能发生的(比如同一帧内
        // 多次触发上传), 这种情况下只有最后一份数据有意义, 直接替换掉旧的待上传条目,
        // 保证同一个 slice 在 mPendingUploads 里最多只有一条, flushUploads 也就不会对同一块
        // 区域发出两次拷贝
        auto it = std::find_if(mPendingUploads.begin(), mPendingUploads.end(),
            [&](const PendingUpload& upload)
            {
                return upload.slice.mipLevel == mipLevel
                    && upload.slice.baseLayer == baseLayer
                    && upload.slice.layerCount == layerCount;
            }
        );

        PendingUpload& pendingUpload = (it != mPendingUploads.end()) ? *it : mPendingUploads.emplace_back();
        pendingUpload.slice.aspect = rhi::TextureAspect::Color;
        pendingUpload.slice.mipLevel = mipLevel;
        pendingUpload.slice.baseLayer = baseLayer;
        pendingUpload.slice.layerCount = layerCount;
        pendingUpload.stagingAllocation.reset();
        pendingUpload.data = std::move(data);
        markNeedsPrepare();

        rhi::TextureFormatBlockInfo blockInfo = rhi::getTextureFormatBlockInfo(mTexture->getDesc().format);
        uint3 baseExtent = mTexture->getDesc().extent;
        uint3 textureExtent = {
            std::max(1u, baseExtent.x >> mipLevel),
            std::max(1u, baseExtent.y >> mipLevel),
            std::max(1u, baseExtent.z >> mipLevel)
        };
        uint32_t blocksPerRow = (textureExtent.x + blockInfo.blockWidth - 1) / blockInfo.blockWidth;
        uint32_t bytesPerRow = blocksPerRow * blockInfo.blockSizeInBytes;
        if (bytesPerRow % 256 == 0)
            return;

        // 对于每行字节大小不是256的倍数, 需要对齐
        // 注意: 数据按 [layer][z][row] 的顺序排列, 需要处理所有 layer
        uint32_t alignedBytesPerRow = (bytesPerRow + 255) & ~255;
        uint32_t rowPerTexture = (textureExtent.y + blockInfo.blockHeight - 1) / blockInfo.blockHeight;
        uint32_t imagesPerLayer = textureExtent.z;
        uint32_t totalImages = layerCount * imagesPerLayer;
        std::vector<uint8_t> alignedData(static_cast<size_t>(alignedBytesPerRow) * rowPerTexture * totalImages);
        for (uint32_t layer = 0; layer < layerCount; ++layer)
        {
            for (uint32_t z = 0; z < textureExtent.z; ++z)
            {
                uint32_t imageIndex = layer * imagesPerLayer + z;
                for (uint32_t y = 0; y < rowPerTexture; ++y)
                {
                    uint8_t* src = pendingUpload.data.data() + (static_cast<size_t>(imageIndex) * rowPerTexture + y) * bytesPerRow;
                    uint8_t* dst = alignedData.data() + (static_cast<size_t>(imageIndex) * rowPerTexture + y) * alignedBytesPerRow;
                    std::memcpy(dst, src, bytesPerRow);
                }
            }
        }
        pendingUpload.data.swap(alignedData);
    }

    bool TextureProxy::prepare(rhi::CommandList& cmdList)
    {
        if (!mInitialized)
        {
            rhi::TextureBarrier barrier;
            barrier.texture = mTexture;
            barrier.range = mTextureView->getDesc().range;
            barrier.oldState = rhi::ResourceState::None;
            barrier.newState = rhi::ResourceState::Shader_Sampled;
            cmdList.insertBarriers({}, { &barrier, 1 });

            mInitialized = true;
        }

        if (!mPendingUploads.empty())
        {
            flushUploads(cmdList);
        }
        return mPendingUploads.empty();
    }

    void TextureProxy::flushUploads(rhi::CommandList& cmdList)
    {
        assert(!mPendingUploads.empty());

        auto& stagingAllocator = getRenderBackend().getStagingAllocator();
        size_t stagedCount = 0;
        for (auto& upload : mPendingUploads)
        {
            upload.stagingAllocation = stagingAllocator.tryAllocate(upload.data.size());
            if (!upload.stagingAllocation)
            {
                // 本帧内可用Staging Buffer已耗尽, 等待下一帧再上传
                break;
            }

            std::memcpy(upload.stagingAllocation->mappedData, upload.data.data(), upload.data.size());
            upload.data.clear();
            upload.data.shrink_to_fit();
            stagedCount++;
        }

        if (stagedCount == 0)
            return;

        rhi::TextureBarrier preBarrier;
        preBarrier.texture = mTexture;
        preBarrier.range = mTextureView->getDesc().range;
        preBarrier.oldState = rhi::ResourceState::Shader_Sampled;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({}, { &preBarrier, 1 });

        rhi::TextureFormatBlockInfo blockInfo = rhi::getTextureFormatBlockInfo(mTexture->getDesc().format);

        for (const auto& upload : mPendingUploads)
        {
            if (!upload.stagingAllocation)
                break;

            auto& allocation = upload.stagingAllocation.value();

            rhi::BufferTextureCopyRegion region;
            uint32_t mipLevel = upload.slice.mipLevel;
            uint3 baseExtent = mTexture->getDesc().extent;
            uint3 textureExtent = {
                std::max(1u, baseExtent.x >> mipLevel),
                std::max(1u, baseExtent.y >> mipLevel),
                std::max(1u, baseExtent.z >> mipLevel)
            };
            region.bufferOffset = allocation.offset;
            uint32_t blocksPerRow = (textureExtent.x + blockInfo.blockWidth - 1) / blockInfo.blockWidth;
            region.bytesPerRow = blocksPerRow * blockInfo.blockSizeInBytes;
            // 向上对齐到 256 bytes
            region.bytesPerRow = (region.bytesPerRow + 255) & ~255;
            region.rowsPerTexture = (textureExtent.y + blockInfo.blockHeight - 1) / blockInfo.blockHeight;
            region.slice = upload.slice;
            region.textureOffset = { 0, 0, 0 };
            region.textureExtent = textureExtent;
            cmdList.copyBufferToTexture(
                allocation.buffer,
                mTexture,
                { &region, 1 }
            );
        }

        mPendingUploads.erase(mPendingUploads.begin(), mPendingUploads.begin() + stagedCount);

        rhi::TextureBarrier postBarrier;
        postBarrier.texture = mTexture;
        postBarrier.range = mTextureView->getDesc().range;
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Sampled;
        cmdList.insertBarriers({}, { &postBarrier, 1 });
    }

}
