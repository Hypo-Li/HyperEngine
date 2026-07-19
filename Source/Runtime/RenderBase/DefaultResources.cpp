#include "DefaultResources.h"
#include "RenderBackend.h"
#include "StagingAllocator.h"

#include <Core/Common/LogSystem.h>

#include <dds.hpp>

#include <cstring>
#include <string>

namespace hyp
{
    namespace
    {
        // WebGPU 要求 copyBufferToTexture 的 bytesPerRow 必须是 256 的整数倍(Vulkan 没有这个
        // 限制, 之前只在 Vulkan 上测过没暴露出来)——这里的占位纹理都只有 1x1/极小尺寸, 实际一行
        // 数据远小于 256 字节, 必须把 staging buffer 里每一行按 256 对齐着摆放, 只在每行开头写
        // 真正的像素数据, 复制时 bytesPerRow 传对齐后的值
        static constexpr uint32_t kTextureRowAlignment = 256;

        constexpr uint32_t alignUp(uint32_t value, uint32_t alignment)
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        // Resource/DfgLut.dds 用 ThirdParty/dds_image 读, 不用自己写 DDS 解析器。只认
        // DXGI_FORMAT_R16G16_FLOAT 这一种格式(跟 evaluateIBL() 期望的 (scale, bias) LUT 对应),
        // 其它格式当成加载失败处理
        dds::Image loadDfgLutFile(const char* path)
        {
            dds::Image image;
            dds::ReadResult result = dds::readFile(std::string(path), &image);
            if (result != dds::ReadResult::Success)
            {
                LOG_ERROR("Failed to read DFG LUT file: {}", path);
                return {};
            }
            if (image.format != DXGI_FORMAT_R16G16_FLOAT || image.mipmaps.empty())
            {
                LOG_ERROR("DFG LUT is not R16G16_FLOAT: {}", path);
                return {};
            }
            return image;
        }
    }

    DefaultResources::DefaultResources(RenderBackend& renderBackend) :
        mRenderBackend(renderBackend)
    {
    }

    void DefaultResources::ensureCreated(rhi::CommandList& cmdList)
    {
        if (mCreated)
            return;
        mCreated = true;

        uint8_t whiteColor[4] = { 255, 255, 255, 255 };
        createSolidColorTexture(cmdList, whiteColor, mWhiteTexture, mWhiteTextureView);

        uint8_t blackColor[4] = { 0, 0, 0, 255 };
        createSolidColorTexture(cmdList, blackColor, mBlackTexture, mBlackTextureView);

        createBlackTexture3D(cmdList);
        createBlackTextureCube(cmdList);
        createDummyUniformBuffer(cmdList);
        createDfgLutTexture(cmdList);
    }

    void DefaultResources::createSolidColorTexture(rhi::CommandList& cmdList, const uint8_t (&color)[4], TRef<rhi::Texture>& outTexture, TRef<rhi::TextureView>& outView)
    {
        auto& device = mRenderBackend.getDevice();
        auto& stagingAllocator = mRenderBackend.getStagingAllocator();

        rhi::TextureDesc desc;
        desc.type = rhi::TextureType::Texture_2D;
        desc.format = rhi::TextureFormat::RGBA8_UNORM;
        desc.extent = { 1, 1, 1 };
        desc.mipCount = 1;
        desc.layerCount = 1;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Transfer_Dst;
        outTexture = device.createTexture(desc, rhi::MemoryType::Gpu_Only);
        outView = outTexture->createView(rhi::makeTextureDefaultViewDesc(desc));

        uint32_t bytesPerRow = alignUp(sizeof(color), kTextureRowAlignment);
        auto allocation = stagingAllocator.forceAllocate(bytesPerRow);
        std::memcpy(allocation.mappedData, color, sizeof(color));

        rhi::TextureBarrier preBarrier;
        preBarrier.texture = outTexture.get();
        preBarrier.range = { rhi::TextureAspect::Color, 0, 1, 0, 1 };
        preBarrier.oldState = rhi::ResourceState::None;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({}, { &preBarrier, 1 });

        rhi::BufferTextureCopyRegion copyRegion;
        copyRegion.bufferOffset = allocation.offset;
        copyRegion.bytesPerRow = bytesPerRow;
        copyRegion.rowsPerTexture = 1;
        copyRegion.slice = { rhi::TextureAspect::Color, 0, 0, 1 };
        copyRegion.textureOffset = { 0, 0, 0 };
        copyRegion.textureExtent = { 1, 1, 1 };
        cmdList.copyBufferToTexture(allocation.buffer, outTexture.get(), { &copyRegion, 1 });

        rhi::TextureBarrier postBarrier;
        postBarrier.texture = outTexture.get();
        postBarrier.range = { rhi::TextureAspect::Color, 0, 1, 0, 1 };
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Sampled;
        cmdList.insertBarriers({}, { &postBarrier, 1 });
    }

    void DefaultResources::createBlackTexture3D(rhi::CommandList& cmdList)
    {
        auto& device = mRenderBackend.getDevice();
        auto& stagingAllocator = mRenderBackend.getStagingAllocator();

        rhi::TextureDesc desc;
        desc.type = rhi::TextureType::Texture_3D;
        desc.format = rhi::TextureFormat::RGBA16_SFLOAT;
        desc.extent = { 1, 1, 1 };
        desc.mipCount = 1;
        desc.layerCount = 1;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Transfer_Dst;
        mBlackTexture3D = device.createTexture(desc, rhi::MemoryType::Gpu_Only);
        mBlackTexture3DView = mBlackTexture3D->createView(rhi::makeTextureDefaultViewDesc(desc));

        uint16_t blackTexel[4] = { 0, 0, 0, 0 };
        uint32_t bytesPerRow = alignUp(sizeof(blackTexel), kTextureRowAlignment);
        auto allocation = stagingAllocator.forceAllocate(bytesPerRow);
        std::memcpy(allocation.mappedData, blackTexel, sizeof(blackTexel));

        rhi::TextureBarrier preBarrier;
        preBarrier.texture = mBlackTexture3D.get();
        preBarrier.range = { rhi::TextureAspect::Color, 0, 1, 0, 1 };
        preBarrier.oldState = rhi::ResourceState::None;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({}, { &preBarrier, 1 });

        rhi::BufferTextureCopyRegion copyRegion;
        copyRegion.bufferOffset = allocation.offset;
        copyRegion.bytesPerRow = bytesPerRow;
        copyRegion.rowsPerTexture = 1;
        copyRegion.slice = { rhi::TextureAspect::Color, 0, 0, 1 };
        copyRegion.textureOffset = { 0, 0, 0 };
        copyRegion.textureExtent = { 1, 1, 1 };
        cmdList.copyBufferToTexture(allocation.buffer, mBlackTexture3D.get(), { &copyRegion, 1 });

        rhi::TextureBarrier postBarrier;
        postBarrier.texture = mBlackTexture3D.get();
        postBarrier.range = { rhi::TextureAspect::Color, 0, 1, 0, 1 };
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Sampled;
        cmdList.insertBarriers({}, { &postBarrier, 1 });
    }

    void DefaultResources::createBlackTextureCube(rhi::CommandList& cmdList)
    {
        auto& device = mRenderBackend.getDevice();
        auto& stagingAllocator = mRenderBackend.getStagingAllocator();

        rhi::TextureDesc desc;
        desc.type = rhi::TextureType::Texture_Cube;
        desc.format = rhi::TextureFormat::RGBA16_SFLOAT;
        desc.extent = { 1, 1, 1 };
        desc.mipCount = 1;
        desc.layerCount = 6;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Transfer_Dst;
        mBlackTextureCube = device.createTexture(desc, rhi::MemoryType::Gpu_Only);
        mBlackTextureCubeView = mBlackTextureCube->createView(rhi::makeTextureDefaultViewDesc(desc));

        uint16_t blackTexel[4] = { 0, 0, 0, 0 };
        uint32_t bytesPerRow = alignUp(sizeof(blackTexel), kTextureRowAlignment);
        auto allocation = stagingAllocator.forceAllocate(bytesPerRow * 6);
        for (uint32_t layer = 0; layer < 6; ++layer)
        {
            std::memcpy(static_cast<uint8_t*>(allocation.mappedData) + layer * bytesPerRow, blackTexel, sizeof(blackTexel));
        }

        rhi::TextureBarrier preBarrier;
        preBarrier.texture = mBlackTextureCube.get();
        preBarrier.range = { rhi::TextureAspect::Color, 0, 1, 0, 6 };
        preBarrier.oldState = rhi::ResourceState::None;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({}, { &preBarrier, 1 });

        rhi::BufferTextureCopyRegion copyRegion;
        copyRegion.bufferOffset = allocation.offset;
        copyRegion.bytesPerRow = bytesPerRow;
        copyRegion.rowsPerTexture = 1;
        copyRegion.slice = { rhi::TextureAspect::Color, 0, 0, 6 };
        copyRegion.textureOffset = { 0, 0, 0 };
        copyRegion.textureExtent = { 1, 1, 1 };
        cmdList.copyBufferToTexture(allocation.buffer, mBlackTextureCube.get(), { &copyRegion, 1 });

        rhi::TextureBarrier postBarrier;
        postBarrier.texture = mBlackTextureCube.get();
        postBarrier.range = { rhi::TextureAspect::Color, 0, 1, 0, 6 };
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Sampled;
        cmdList.insertBarriers({}, { &postBarrier, 1 });
    }

    void DefaultResources::createDummyUniformBuffer(rhi::CommandList& cmdList)
    {
        auto& device = mRenderBackend.getDevice();
        auto& stagingAllocator = mRenderBackend.getStagingAllocator();

        rhi::BufferDesc bufferDesc;
        bufferDesc.size = kDummyUniformBufferSize;
        bufferDesc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::Transfer_Dst;
        mDummyUniformBuffer = device.createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);

        auto allocation = stagingAllocator.forceAllocate(kDummyUniformBufferSize);
        std::memset(allocation.mappedData, 0, kDummyUniformBufferSize);

        rhi::BufferBarrier preBarrier;
        preBarrier.buffer = mDummyUniformBuffer.get();
        preBarrier.offset = 0;
        preBarrier.size = kDummyUniformBufferSize;
        preBarrier.oldState = rhi::ResourceState::None;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({ &preBarrier, 1 });

        rhi::BufferCopyRegion copyRegion;
        copyRegion.srcOffset = allocation.offset;
        copyRegion.dstOffset = 0;
        copyRegion.size = kDummyUniformBufferSize;
        cmdList.copyBuffer(allocation.buffer, mDummyUniformBuffer.get(), { &copyRegion, 1 });

        rhi::BufferBarrier postBarrier;
        postBarrier.buffer = mDummyUniformBuffer.get();
        postBarrier.offset = 0;
        postBarrier.size = kDummyUniformBufferSize;
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Uniform;
        cmdList.insertBarriers({ &postBarrier, 1 });
    }

    void DefaultResources::createDfgLutTexture(rhi::CommandList& cmdList)
    {
        dds::Image lutImage = loadDfgLutFile(RESOURCE_DIR "/DfgLut.dds");
        assert(!lutImage.mipmaps.empty());
        if (lutImage.mipmaps.empty())
            return;

        auto& device = mRenderBackend.getDevice();
        auto& stagingAllocator = mRenderBackend.getStagingAllocator();

        rhi::TextureDesc lutDesc;
        lutDesc.type = rhi::TextureType::Texture_2D;
        lutDesc.format = rhi::TextureFormat::RG16_SFLOAT;
        lutDesc.extent = { lutImage.width, lutImage.height, 1 };
        lutDesc.mipCount = 1;
        lutDesc.layerCount = 1;
        lutDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Transfer_Dst;
        mDfgLutTexture = device.createTexture(lutDesc, rhi::MemoryType::Gpu_Only);
        mDfgLutView = mDfgLutTexture->createView(rhi::makeTextureDefaultViewDesc(lutDesc));

        const dds::span<uint8_t>& mip0 = lutImage.mipmaps[0];
        auto allocation = stagingAllocator.forceAllocate(mip0.size_bytes());
        std::memcpy(allocation.mappedData, mip0.data(), mip0.size_bytes());

        rhi::TextureBarrier preBarrier;
        preBarrier.texture = mDfgLutTexture.get();
        preBarrier.range = { rhi::TextureAspect::Color, 0, 1, 0, 1 };
        preBarrier.oldState = rhi::ResourceState::None;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({}, { &preBarrier, 1 });

        rhi::BufferTextureCopyRegion copyRegion;
        copyRegion.bufferOffset = allocation.offset;
        copyRegion.bytesPerRow = lutImage.width * 4;
        copyRegion.rowsPerTexture = lutImage.height;
        copyRegion.slice = { rhi::TextureAspect::Color, 0, 0, 1 };
        copyRegion.textureOffset = { 0, 0, 0 };
        copyRegion.textureExtent = { lutImage.width, lutImage.height, 1 };
        cmdList.copyBufferToTexture(allocation.buffer, mDfgLutTexture.get(), { &copyRegion, 1 });

        rhi::TextureBarrier postBarrier;
        postBarrier.texture = mDfgLutTexture.get();
        postBarrier.range = { rhi::TextureAspect::Color, 0, 1, 0, 1 };
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Sampled;
        cmdList.insertBarriers({}, { &postBarrier, 1 });
    }
}
