#include "DefaultResources.h"
#include "RenderBackend.h"
#include "StagingAllocator.h"

#include <Core/Common/LogSystem.h>
#include <Core/Math/Math.h>
#include <Core/Math/Vector.h>

#include <dds.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>
#include <string>
#include <vector>

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
        // DXGI_FORMAT_R16G16B16A16_FLOAT 这一种格式(RG 是 evaluateIBL() 期望的标准 GGX
        // (scale, bias) LUT, BA 是 Cloth shading model 专用的同构 LUT), 其它格式当成加载失败处理
        dds::Image loadDfgLutFile(const char* path)
        {
            dds::Image image;
            dds::ReadResult result = dds::readFile(std::string(path), &image);
            if (result != dds::ReadResult::Success)
            {
                LOG_ERROR("Failed to read DFG LUT file: {}", path);
                return {};
            }
            if (image.format != DXGI_FORMAT_R16G16B16A16_FLOAT || image.mipmaps.empty())
            {
                LOG_ERROR("DFG LUT is not R16G16B16A16_FLOAT: {}", path);
                return {};
            }
            return image;
        }

        // Resource/FastBlueNoise_vec2_128x128x64.dds 是从 UE 自带的同名蓝噪声纹理提取出来的
        // 原始像素数据, 没有 DX10 头, 用传统的 RGB bitmask 描述格式——掩码对应 dds_image 识别出
        // 的 DXGI_FORMAT_B8G8R8A8_UNORM(内存字节序 B,G,R,A), .rg 通道(逻辑 R/G, 对应内存里第
        // 2/1 字节)是蓝噪声二维向量, .ba 恒定未使用
        dds::Image loadBlueNoiseVec2File(const char* path)
        {
            dds::Image image;
            dds::ReadResult result = dds::readFile(std::string(path), &image);
            if (result != dds::ReadResult::Success)
            {
                LOG_ERROR("Failed to read blue noise texture file: {}", path);
                return {};
            }
            if (image.format != DXGI_FORMAT_B8G8R8A8_UNORM || image.mipmaps.empty())
            {
                LOG_ERROR("Blue noise texture is not B8G8R8A8_UNORM: {}", path);
                return {};
            }
            return image;
        }
    }

    DefaultResources::DefaultResources(RenderBackend& renderBackend, const std::filesystem::path& resourceDir) :
        mRenderBackend(renderBackend),
        mResourceDir(resourceDir)
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
        createDummyStorageBuffer(cmdList);
        createDfgLutTexture(cmdList);
        createBlueNoiseVec2Texture(cmdList);
        createVisualizationWireframeBoxGeometry(cmdList);
        createVisualizationWireframeSphereGeometry(cmdList);
        createVisualizationWireframeCapsuleGeometry(cmdList);
        createVisualizationWireframeCylinderGeometry(cmdList);
        createVisualizationWireframeLightConeGeometry(cmdList);
        createVisualizationIconAtlas(cmdList);
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

    void DefaultResources::createDummyStorageBuffer(rhi::CommandList& cmdList)
    {
        auto& device = mRenderBackend.getDevice();
        auto& stagingAllocator = mRenderBackend.getStagingAllocator();

        rhi::BufferDesc bufferDesc;
        bufferDesc.size = kDummyStorageBufferSize;
        bufferDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Dst;
        mDummyStorageBuffer = device.createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);

        auto allocation = stagingAllocator.forceAllocate(kDummyStorageBufferSize);
        std::memset(allocation.mappedData, 0, kDummyStorageBufferSize);

        rhi::BufferBarrier preBarrier;
        preBarrier.buffer = mDummyStorageBuffer.get();
        preBarrier.offset = 0;
        preBarrier.size = kDummyStorageBufferSize;
        preBarrier.oldState = rhi::ResourceState::None;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({ &preBarrier, 1 });

        rhi::BufferCopyRegion copyRegion;
        copyRegion.srcOffset = allocation.offset;
        copyRegion.dstOffset = 0;
        copyRegion.size = kDummyStorageBufferSize;
        cmdList.copyBuffer(allocation.buffer, mDummyStorageBuffer.get(), { &copyRegion, 1 });

        rhi::BufferBarrier postBarrier;
        postBarrier.buffer = mDummyStorageBuffer.get();
        postBarrier.offset = 0;
        postBarrier.size = kDummyStorageBufferSize;
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Storage;
        cmdList.insertBarriers({ &postBarrier, 1 });
    }

    void DefaultResources::createDfgLutTexture(rhi::CommandList& cmdList)
    {
        dds::Image lutImage = loadDfgLutFile((mResourceDir / "DfgLut.dds").string().c_str());
        assert(!lutImage.mipmaps.empty());
        if (lutImage.mipmaps.empty())
            return;

        auto& device = mRenderBackend.getDevice();
        auto& stagingAllocator = mRenderBackend.getStagingAllocator();

        rhi::TextureDesc lutDesc;
        lutDesc.type = rhi::TextureType::Texture_2D;
        lutDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
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
        copyRegion.bytesPerRow = lutImage.width * 8;
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

    void DefaultResources::createBlueNoiseVec2Texture(rhi::CommandList& cmdList)
    {
        dds::Image image = loadBlueNoiseVec2File((mResourceDir / "FastBlueNoise_vec2_128x128x64.dds").string().c_str());
        assert(!image.mipmaps.empty());
        if (image.mipmaps.empty())
            return;

        auto& device = mRenderBackend.getDevice();
        auto& stagingAllocator = mRenderBackend.getStagingAllocator();

        rhi::TextureDesc desc;
        desc.type = rhi::TextureType::Texture_2D;
        desc.format = rhi::TextureFormat::BGRA8_UNORM;
        desc.extent = { image.width, image.height, 1 };
        desc.mipCount = 1;
        desc.layerCount = 1;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Transfer_Dst;
        mBlueNoiseVec2Texture = device.createTexture(desc, rhi::MemoryType::Gpu_Only);
        mBlueNoiseVec2View = mBlueNoiseVec2Texture->createView(rhi::makeTextureDefaultViewDesc(desc));

        const dds::span<uint8_t>& mip0 = image.mipmaps[0];
        auto allocation = stagingAllocator.forceAllocate(mip0.size_bytes());
        std::memcpy(allocation.mappedData, mip0.data(), mip0.size_bytes());

        rhi::TextureBarrier preBarrier;
        preBarrier.texture = mBlueNoiseVec2Texture.get();
        preBarrier.range = { rhi::TextureAspect::Color, 0, 1, 0, 1 };
        preBarrier.oldState = rhi::ResourceState::None;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({}, { &preBarrier, 1 });

        rhi::BufferTextureCopyRegion copyRegion;
        copyRegion.bufferOffset = allocation.offset;
        copyRegion.bytesPerRow = image.width * 4;
        copyRegion.rowsPerTexture = image.height;
        copyRegion.slice = { rhi::TextureAspect::Color, 0, 0, 1 };
        copyRegion.textureOffset = { 0, 0, 0 };
        copyRegion.textureExtent = { image.width, image.height, 1 };
        cmdList.copyBufferToTexture(allocation.buffer, mBlueNoiseVec2Texture.get(), { &copyRegion, 1 });

        rhi::TextureBarrier postBarrier;
        postBarrier.texture = mBlueNoiseVec2Texture.get();
        postBarrier.range = { rhi::TextureAspect::Color, 0, 1, 0, 1 };
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Sampled;
        cmdList.insertBarriers({}, { &postBarrier, 1 });
    }

    namespace
    {
        // vertex 段在前、index 段按16字节对齐紧跟其后拼进同一个 buffer, 跟 GeometryProxy 的
        // 单 buffer 布局同一个约定; 上传/收尾状态转换也是同一套(Transfer_Dst -> Vertex_
        // Attachment|Index_Attachment)
        TRef<rhi::Buffer> uploadGeometryBuffer(
            RenderBackend& renderBackend,
            rhi::CommandList& cmdList,
            std::span<const float3> vertices,
            std::span<const uint16_t> indices,
            uint64_t& outIndexOffset
        )
        {
            auto& device = renderBackend.getDevice();
            auto& stagingAllocator = renderBackend.getStagingAllocator();

            uint64_t vertexDataSize = vertices.size() * sizeof(float3);
            uint64_t indexDataSize = indices.size() * sizeof(uint16_t);
            uint64_t indexOffset = (vertexDataSize + 15) & ~uint64_t(15);
            outIndexOffset = indexOffset;

            rhi::BufferDesc bufferDesc;
            bufferDesc.size = indexOffset + indexDataSize;
            bufferDesc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::Index | rhi::BufferUsage::Transfer_Dst;
            TRef<rhi::Buffer> buffer = device.createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);

            auto vertexAllocation = stagingAllocator.forceAllocate(vertexDataSize);
            std::memcpy(vertexAllocation.mappedData, vertices.data(), vertexDataSize);
            auto indexAllocation = stagingAllocator.forceAllocate(indexDataSize);
            std::memcpy(indexAllocation.mappedData, indices.data(), indexDataSize);

            rhi::BufferBarrier preBarrier;
            preBarrier.buffer = buffer.get();
            preBarrier.offset = 0;
            preBarrier.size = rhi::kWholeBuffer;
            preBarrier.oldState = rhi::ResourceState::None;
            preBarrier.newState = rhi::ResourceState::Transfer_Dst;
            cmdList.insertBarriers({ &preBarrier, 1 });

            rhi::BufferCopyRegion vertexCopyRegion;
            vertexCopyRegion.srcOffset = vertexAllocation.offset;
            vertexCopyRegion.dstOffset = 0;
            vertexCopyRegion.size = vertexDataSize;
            cmdList.copyBuffer(vertexAllocation.buffer, buffer.get(), { &vertexCopyRegion, 1 });

            rhi::BufferCopyRegion indexCopyRegion;
            indexCopyRegion.srcOffset = indexAllocation.offset;
            indexCopyRegion.dstOffset = indexOffset;
            indexCopyRegion.size = indexDataSize;
            cmdList.copyBuffer(indexAllocation.buffer, buffer.get(), { &indexCopyRegion, 1 });

            rhi::BufferBarrier postBarrier;
            postBarrier.buffer = buffer.get();
            postBarrier.offset = 0;
            postBarrier.size = rhi::kWholeBuffer;
            postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
            postBarrier.newState = rhi::ResourceState::Vertex_Attachment | rhi::ResourceState::Index_Attachment;
            cmdList.insertBarriers({ &postBarrier, 1 });

            return buffer;
        }
    }

    void DefaultResources::createVisualizationWireframeBoxGeometry(rhi::CommandList& cmdList)
    {
        // 单位盒: -0.5~0.5, VisualizationPass 按每个 instance 的 halfExtents*2 做非均匀缩放
        std::vector<float3> vertices = {
            { -0.5f, -0.5f, -0.5f }, { 0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, -0.5f }, { -0.5f, 0.5f, -0.5f },
            { -0.5f, -0.5f, 0.5f }, { 0.5f, -0.5f, 0.5f }, { 0.5f, 0.5f, 0.5f }, { -0.5f, 0.5f, 0.5f },
        };
        std::vector<uint16_t> indices = {
            0, 1, 1, 2, 2, 3, 3, 0, // 底面四条边
            4, 5, 5, 6, 6, 7, 7, 4, // 顶面四条边
            0, 4, 1, 5, 2, 6, 3, 7  // 四条竖边
        };
        mVisualizationWireframeBoxIndexCount = static_cast<uint32_t>(indices.size());
        mVisualizationWireframeBoxBuffer = uploadGeometryBuffer(mRenderBackend, cmdList, vertices, indices, mVisualizationWireframeBoxIndexOffset);
    }

    void DefaultResources::createVisualizationWireframeSphereGeometry(rhi::CommandList& cmdList)
    {
        // 三条正交单位大圆环(半径 1), 不是完整三角剖分的球面——这是编辑器线框球 gizmo 的
        // 标准画法, VisualizationPass 按 instance 的 radius 做统一缩放(球在任意朝向下都一样,
        // 不需要非均匀缩放)
        constexpr uint32_t N = kVisualizationWireframeSphereSegments;
        std::vector<float3> vertices;
        vertices.reserve(N * 3);
        for (uint32_t i = 0; i < N; ++i)
        {
            float angle = static_cast<float>(i) * (2.0f * Math::PI / static_cast<float>(N));
            vertices.emplace_back(std::cos(angle), std::sin(angle), 0.0f); // XY 平面
        }
        for (uint32_t i = 0; i < N; ++i)
        {
            float angle = static_cast<float>(i) * (2.0f * Math::PI / static_cast<float>(N));
            vertices.emplace_back(std::cos(angle), 0.0f, std::sin(angle)); // XZ 平面
        }
        for (uint32_t i = 0; i < N; ++i)
        {
            float angle = static_cast<float>(i) * (2.0f * Math::PI / static_cast<float>(N));
            vertices.emplace_back(0.0f, std::cos(angle), std::sin(angle)); // YZ 平面
        }

        std::vector<uint16_t> indices;
        indices.reserve(N * 3 * 2);
        for (uint32_t ring = 0; ring < 3; ++ring)
        {
            uint32_t base = ring * N;
            for (uint32_t i = 0; i < N; ++i)
            {
                indices.push_back(static_cast<uint16_t>(base + i));
                indices.push_back(static_cast<uint16_t>(base + (i + 1) % N));
            }
        }
        mVisualizationWireframeSphereIndexCount = static_cast<uint32_t>(indices.size());
        mVisualizationWireframeSphereBuffer = uploadGeometryBuffer(mRenderBackend, cmdList, vertices, indices, mVisualizationWireframeSphereIndexOffset);
    }

    void DefaultResources::createVisualizationWireframeCapsuleGeometry(rhi::CommandList& cmdList)
    {
        // 圆柱段半高=1、半径=1, 轴向 +Z; 两端半球盖各用两条正交半圆弧(从圆柱段圆环扫到极点)。
        // VisualizationPass 分别用 radius 缩径向、halfHeight 缩轴向——半球盖那部分因此跟
        // LightVolumeCone 的球形封口一样, 只在 radius==halfHeight 时精确, 其余比例下是近似
        constexpr uint32_t N = kVisualizationWireframeCylinderSegments;
        constexpr uint32_t K = kVisualizationWireframeCylinderSideLines;
        constexpr uint32_t M = kVisualizationWireframeCapsuleSegments;
        constexpr float halfHeight = 1.0f;
        constexpr float radius = 1.0f;

        std::vector<float3> vertices;
        uint32_t topRingBase = static_cast<uint32_t>(vertices.size());
        for (uint32_t i = 0; i < N; ++i)
        {
            float angle = static_cast<float>(i) * (2.0f * Math::PI / static_cast<float>(N));
            vertices.emplace_back(radius * std::cos(angle), radius * std::sin(angle), halfHeight);
        }
        uint32_t bottomRingBase = static_cast<uint32_t>(vertices.size());
        for (uint32_t i = 0; i < N; ++i)
        {
            float angle = static_cast<float>(i) * (2.0f * Math::PI / static_cast<float>(N));
            vertices.emplace_back(radius * std::cos(angle), radius * std::sin(angle), -halfHeight);
        }
        uint32_t topArcXBase = static_cast<uint32_t>(vertices.size());
        for (uint32_t i = 0; i <= M; ++i)
        {
            float t = static_cast<float>(i) * (Math::PI / static_cast<float>(M));
            vertices.emplace_back(radius * std::cos(t), 0.0f, halfHeight + radius * std::sin(t));
        }
        uint32_t topArcYBase = static_cast<uint32_t>(vertices.size());
        for (uint32_t i = 0; i <= M; ++i)
        {
            float t = static_cast<float>(i) * (Math::PI / static_cast<float>(M));
            vertices.emplace_back(0.0f, radius * std::cos(t), halfHeight + radius * std::sin(t));
        }
        uint32_t bottomArcXBase = static_cast<uint32_t>(vertices.size());
        for (uint32_t i = 0; i <= M; ++i)
        {
            float t = static_cast<float>(i) * (Math::PI / static_cast<float>(M));
            vertices.emplace_back(radius * std::cos(t), 0.0f, -halfHeight - radius * std::sin(t));
        }
        uint32_t bottomArcYBase = static_cast<uint32_t>(vertices.size());
        for (uint32_t i = 0; i <= M; ++i)
        {
            float t = static_cast<float>(i) * (Math::PI / static_cast<float>(M));
            vertices.emplace_back(0.0f, radius * std::cos(t), -halfHeight - radius * std::sin(t));
        }

        std::vector<uint16_t> indices;
        for (uint32_t i = 0; i < N; ++i)
        {
            indices.push_back(static_cast<uint16_t>(topRingBase + i));
            indices.push_back(static_cast<uint16_t>(topRingBase + (i + 1) % N));
        }
        for (uint32_t i = 0; i < N; ++i)
        {
            indices.push_back(static_cast<uint16_t>(bottomRingBase + i));
            indices.push_back(static_cast<uint16_t>(bottomRingBase + (i + 1) % N));
        }
        for (uint32_t k = 0; k < K; ++k)
        {
            uint32_t i = (k * N) / K;
            indices.push_back(static_cast<uint16_t>(topRingBase + i));
            indices.push_back(static_cast<uint16_t>(bottomRingBase + i));
        }
        auto appendArc = [&indices](uint32_t base, uint32_t segments)
        {
            for (uint32_t i = 0; i < segments; ++i)
            {
                indices.push_back(static_cast<uint16_t>(base + i));
                indices.push_back(static_cast<uint16_t>(base + i + 1));
            }
        };
        appendArc(topArcXBase, M);
        appendArc(topArcYBase, M);
        appendArc(bottomArcXBase, M);
        appendArc(bottomArcYBase, M);

        mVisualizationWireframeCapsuleIndexCount = static_cast<uint32_t>(indices.size());
        mVisualizationWireframeCapsuleBuffer = uploadGeometryBuffer(mRenderBackend, cmdList, vertices, indices, mVisualizationWireframeCapsuleIndexOffset);
    }

    void DefaultResources::createVisualizationWireframeCylinderGeometry(rhi::CommandList& cmdList)
    {
        // 半高=1、半径=1, 轴向 +Z: 顶/底两圈圆环 + K 条纵向连接线
        constexpr uint32_t N = kVisualizationWireframeCylinderSegments;
        constexpr uint32_t K = kVisualizationWireframeCylinderSideLines;

        std::vector<float3> vertices;
        for (uint32_t i = 0; i < N; ++i)
        {
            float angle = static_cast<float>(i) * (2.0f * Math::PI / static_cast<float>(N));
            vertices.emplace_back(std::cos(angle), std::sin(angle), 1.0f);
        }
        for (uint32_t i = 0; i < N; ++i)
        {
            float angle = static_cast<float>(i) * (2.0f * Math::PI / static_cast<float>(N));
            vertices.emplace_back(std::cos(angle), std::sin(angle), -1.0f);
        }

        std::vector<uint16_t> indices;
        for (uint32_t i = 0; i < N; ++i)
        {
            indices.push_back(static_cast<uint16_t>(i));
            indices.push_back(static_cast<uint16_t>((i + 1) % N));
        }
        for (uint32_t i = 0; i < N; ++i)
        {
            indices.push_back(static_cast<uint16_t>(N + i));
            indices.push_back(static_cast<uint16_t>(N + (i + 1) % N));
        }
        for (uint32_t k = 0; k < K; ++k)
        {
            uint32_t i = (k * N) / K;
            indices.push_back(static_cast<uint16_t>(i));
            indices.push_back(static_cast<uint16_t>(N + i));
        }
        mVisualizationWireframeCylinderIndexCount = static_cast<uint32_t>(indices.size());
        mVisualizationWireframeCylinderBuffer = uploadGeometryBuffer(mRenderBackend, cmdList, vertices, indices, mVisualizationWireframeCylinderIndexOffset);
    }

    void DefaultResources::createVisualizationWireframeLightConeGeometry(rhi::CommandList& cmdList)
    {
        // 锥顶在原点, 轴向 -Z(跟 Light.slang 的 evaluateSpotLightVector/LightVolume.slang 现在
        // 用的聚光灯锥轴约定一致, 调用方可以直接拿实体的世界变换摆放, 不需要额外再套一层重映射),
        // 烘焙在 45 度参考角、range=1——形状照抄 LightVolume.slang 的 computeConeLocalPosition():
        // 侧面终止于 zRadius=range*cos(halfAngle) 处的边界圆(半径 baseRadius=range*sin(halfAngle)),
        // 边界圆之外是球心在锥顶、半径 range 的球面被半角裁出的那顶球冠, 不是简化成平底圆盘。
        // 纯线框: N 条从锥顶到边界圆的放射线 + 边界圆本身(闭合的完整圆, 不需要故意断开) + 两条
        // 正交的完整球冠半圆弧——每条弧从边界圆某个方位角(0/90 度)一路穿过锥轴上的球冠顶点、
        // 延伸到边界圆正对面那个方位角(180/270 度), 不是只画到顶点就停(那样两条弧只会在顶点
        // 碰到一个点、互相垂直但不会穿过对方, 看起来不像球冠); 跟 Capsule 封口 topArcX/topArcY
        // 完整半圆弧的画法同一个惯例, 两条弧才会在顶点处像十字一样正确交叉。参数是极角
        // (-halfAngle~halfAngle, 0 处是顶点), 天然精确, 不需要 LightVolumeCone 网格顶点那种
        // sqrt 反解。这里画的是线框(Line_List), 没有三角形环绕方向的顾虑, 直接对 y/z 分量取
        // 负号(等价于 LightVolume.slang 里绕 X 轴转 180 度那个把 +Z 重映射到 -Z 的真旋转),
        // 不需要像那边一样额外操心行列式
        constexpr uint32_t N = kVisualizationWireframeLightConeSides;
        constexpr uint32_t M = kVisualizationWireframeLightConeCapSegments;
        constexpr float halfAngle = Math::PI * 0.25f;
        constexpr float range = 1.0f;
        float zRadius = range * std::cos(halfAngle);
        float baseRadius = range * std::sin(halfAngle);

        std::vector<float3> vertices;
        vertices.emplace_back(0.0f, 0.0f, 0.0f); // 锥顶
        uint32_t baseRingStart = static_cast<uint32_t>(vertices.size());
        for (uint32_t i = 0; i < N; ++i)
        {
            float angle = static_cast<float>(i) * (2.0f * Math::PI / static_cast<float>(N));
            vertices.emplace_back(baseRadius * std::cos(angle), -baseRadius * std::sin(angle), -zRadius);
        }
        uint32_t capArcXBase = static_cast<uint32_t>(vertices.size());
        for (uint32_t i = 0; i <= 2 * M; ++i)
        {
            float t = (static_cast<float>(i) - static_cast<float>(M)) * (halfAngle / static_cast<float>(M));
            vertices.emplace_back(range * std::sin(t), 0.0f, -range * std::cos(t));
        }
        uint32_t capArcYBase = static_cast<uint32_t>(vertices.size());
        for (uint32_t i = 0; i <= 2 * M; ++i)
        {
            float t = (static_cast<float>(i) - static_cast<float>(M)) * (halfAngle / static_cast<float>(M));
            vertices.emplace_back(0.0f, -range * std::sin(t), -range * std::cos(t));
        }

        std::vector<uint16_t> indices;
        for (uint32_t i = 0; i < N; ++i)
        {
            indices.push_back(0);
            indices.push_back(static_cast<uint16_t>(baseRingStart + i));
        }
        for (uint32_t i = 0; i < N; ++i)
        {
            indices.push_back(static_cast<uint16_t>(baseRingStart + i));
            indices.push_back(static_cast<uint16_t>(baseRingStart + (i + 1) % N));
        }
        auto appendArc = [&indices](uint32_t base, uint32_t segments)
        {
            for (uint32_t i = 0; i < segments; ++i)
            {
                indices.push_back(static_cast<uint16_t>(base + i));
                indices.push_back(static_cast<uint16_t>(base + i + 1));
            }
        };
        appendArc(capArcXBase, 2 * M);
        appendArc(capArcYBase, 2 * M);

        mVisualizationWireframeLightConeIndexCount = static_cast<uint32_t>(indices.size());
        mVisualizationWireframeLightConeBuffer = uploadGeometryBuffer(mRenderBackend, cmdList, vertices, indices, mVisualizationWireframeLightConeIndexOffset);
    }

    void DefaultResources::createVisualizationIconAtlas(rhi::CommandList& cmdList)
    {
        auto& device = mRenderBackend.getDevice();
        auto& stagingAllocator = mRenderBackend.getStagingAllocator();

        constexpr uint32_t cellSize = kVisualizationIconAtlasCellSize;
        constexpr uint32_t cellCount = kVisualizationIconAtlasCellCount;
        uint32_t atlasWidth = cellSize * cellCount;
        uint32_t atlasHeight = cellSize;

        rhi::TextureDesc desc;
        desc.type = rhi::TextureType::Texture_2D;
        desc.format = rhi::TextureFormat::RGBA8_UNORM;
        desc.extent = { atlasWidth, atlasHeight, 1 };
        desc.mipCount = 1;
        desc.layerCount = 1;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Transfer_Dst;
        mVisualizationIconAtlasTexture = device.createTexture(desc, rhi::MemoryType::Gpu_Only);
        mVisualizationIconAtlasView = mVisualizationIconAtlasTexture->createView(rhi::makeTextureDefaultViewDesc(desc));

        // 占位纯色, 每个格子一种区分明显的颜色——真实图标美术资源接入后, 只需要整个替换这个
        // 函数内部的填充逻辑, getVisualizationIconAtlasUV() 这个查表接口不受影响
        static constexpr uint8_t kPlaceholderColors[kVisualizationIconAtlasCellCount][4] = {
            { 255, 220, 80, 255 },  // 0
            { 80, 180, 255, 255 },  // 1
            { 255, 140, 220, 255 }, // 2
            { 140, 255, 140, 255 }, // 3
        };

        uint32_t bytesPerRow = alignUp(atlasWidth * 4, kTextureRowAlignment);
        auto allocation = stagingAllocator.forceAllocate(bytesPerRow * atlasHeight);
        for (uint32_t cell = 0; cell < cellCount; ++cell)
        {
            for (uint32_t y = 0; y < cellSize; ++y)
            {
                uint8_t* row = static_cast<uint8_t*>(allocation.mappedData) + y * bytesPerRow + cell * cellSize * 4;
                for (uint32_t x = 0; x < cellSize; ++x)
                    std::memcpy(row + x * 4, kPlaceholderColors[cell], 4);
            }
        }

        rhi::TextureBarrier preBarrier;
        preBarrier.texture = mVisualizationIconAtlasTexture.get();
        preBarrier.range = { rhi::TextureAspect::Color, 0, 1, 0, 1 };
        preBarrier.oldState = rhi::ResourceState::None;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({}, { &preBarrier, 1 });

        rhi::BufferTextureCopyRegion copyRegion;
        copyRegion.bufferOffset = allocation.offset;
        copyRegion.bytesPerRow = bytesPerRow;
        copyRegion.rowsPerTexture = atlasHeight;
        copyRegion.slice = { rhi::TextureAspect::Color, 0, 0, 1 };
        copyRegion.textureOffset = { 0, 0, 0 };
        copyRegion.textureExtent = { atlasWidth, atlasHeight, 1 };
        cmdList.copyBufferToTexture(allocation.buffer, mVisualizationIconAtlasTexture.get(), { &copyRegion, 1 });

        rhi::TextureBarrier postBarrier;
        postBarrier.texture = mVisualizationIconAtlasTexture.get();
        postBarrier.range = { rhi::TextureAspect::Color, 0, 1, 0, 1 };
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Sampled;
        cmdList.insertBarriers({}, { &postBarrier, 1 });
    }

    float4 DefaultResources::getVisualizationIconAtlasUV(uint32_t cellIndex) const noexcept
    {
        assert(cellIndex < kVisualizationIconAtlasCellCount);
        float u0 = static_cast<float>(cellIndex) / static_cast<float>(kVisualizationIconAtlasCellCount);
        float u1 = static_cast<float>(cellIndex + 1) / static_cast<float>(kVisualizationIconAtlasCellCount);
        return float4(u0, 0.0f, u1, 1.0f);
    }
}
