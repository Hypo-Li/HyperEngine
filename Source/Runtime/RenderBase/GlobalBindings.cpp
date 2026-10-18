#include "GlobalBindings.h"
#include "RenderBackend.h"

namespace hyp
{
    static constexpr uint32_t kBindlessBindingOffset = 2; // binding 1 留空, 保持 Binding.slang 里 bindless 数组的 binding 号不变
    static constexpr uint32_t kTexture2DBinding = uint32_t(BindlessBinding::Texture2D) + kBindlessBindingOffset;
    static constexpr uint32_t kTexture2DArrayBinding = uint32_t(BindlessBinding::Texture2DArray) + kBindlessBindingOffset;
    static constexpr uint32_t kTexture3DBinding = uint32_t(BindlessBinding::Texture3D) + kBindlessBindingOffset;
    static constexpr uint32_t kTextureCubeBinding = uint32_t(BindlessBinding::TextureCube) + kBindlessBindingOffset;
    static constexpr uint32_t kSamplerBinding = uint32_t(BindlessBinding::Sampler) + kBindlessBindingOffset;

    GlobalBindings::GlobalBindings(RenderBackend& renderBackend) :
        mRenderBackend(renderBackend),
        mBindlessEnabled(renderBackend.getDevice().getFeatures().bindless)
    {
        if (mBindlessEnabled)
        {
            initBindlessDefaultResources();
            renderBackend.getBindlessRegistry().setGlobalBindings(this);
        }
        initGlobalDataBindGroup(mRenderBackend.getBindlessRegistry().getConfig());
    }

    void GlobalBindings::writeTexture2D(uint32_t index, rhi::TextureView* view)
    {
        assert(mBindlessEnabled);
        rhi::TextureView* actualView = view ? view : mDefaultTexture2DView.get();

        rhi::BindGroupEntry entry;
        entry.binding = kTexture2DBinding;
        entry.type = rhi::BindingType::Sampled_Texture;
        entry.index = index;
        entry.textures = { &actualView, 1 };

        mGlobalDataBindGroup->update({ &entry, 1 });
    }

    void GlobalBindings::writeTexture2DArray(uint32_t index, rhi::TextureView* view)
    {
        assert(mBindlessEnabled);
        rhi::TextureView* actualView = view ? view : mDefaultTexture2DArrayView.get();

        rhi::BindGroupEntry entry;
        entry.binding = kTexture2DArrayBinding;
        entry.type = rhi::BindingType::Sampled_Texture;
        entry.index = index;
        entry.textures = { &actualView, 1 };

        mGlobalDataBindGroup->update({ &entry, 1 });
    }

    void GlobalBindings::writeTexture3D(uint32_t index, rhi::TextureView* view)
    {
        assert(mBindlessEnabled);
        rhi::TextureView* actualView = view ? view : mDefaultTexture3DView.get();

        rhi::BindGroupEntry entry;
        entry.binding = kTexture3DBinding;
        entry.type = rhi::BindingType::Sampled_Texture;
        entry.index = index;
        entry.textures = { &actualView, 1 };

        mGlobalDataBindGroup->update({ &entry, 1 });
    }

    void GlobalBindings::writeTextureCube(uint32_t index, rhi::TextureView* view)
    {
        assert(mBindlessEnabled);
        rhi::TextureView* actualView = view ? view : mDefaultTextureCubeView.get();

        rhi::BindGroupEntry entry;
        entry.binding = kTextureCubeBinding;
        entry.type = rhi::BindingType::Sampled_Texture;
        entry.index = index;
        entry.textures = { &actualView, 1 };

        mGlobalDataBindGroup->update({ &entry, 1 });
    }

    void GlobalBindings::writeSampler(uint32_t index, rhi::Sampler* sampler)
    {
        assert(mBindlessEnabled);
        rhi::Sampler* actualSampler = sampler ? sampler : mDefaultSampler.get();

        rhi::BindGroupEntry entry;
        entry.binding = kSamplerBinding;
        entry.type = rhi::BindingType::Sampler;
        entry.index = index;
        entry.samplers = { &actualSampler, 1 };
        mGlobalDataBindGroup->update({ &entry, 1 });
    }

    void GlobalBindings::updateFrameData(rhi::CommandList& cmdList, const FrameData& frameData)
    {
        rhi::BufferBarrier preBarrier;
        preBarrier.buffer = mFrameDataUniformBuffer.get();
        preBarrier.offset = 0;
        preBarrier.size = sizeof(FrameData);
        preBarrier.oldState = rhi::ResourceState::Shader_Uniform;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({ &preBarrier, 1 });

        auto allocation = mRenderBackend.getStagingAllocator().forceAllocate(sizeof(FrameData));
        std::memcpy(allocation.mappedData, &frameData, sizeof(FrameData));
        rhi::BufferCopyRegion region;
        region.srcOffset = allocation.offset;
        region.dstOffset = 0;
        region.size = sizeof(FrameData);
        cmdList.copyBuffer(allocation.buffer, mFrameDataUniformBuffer.get(), { &region, 1 });

        rhi::BufferBarrier postBarrier;
        postBarrier.buffer = mFrameDataUniformBuffer.get();
        postBarrier.offset = 0;
        postBarrier.size = sizeof(FrameData);
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Uniform;
        cmdList.insertBarriers({ &postBarrier, 1 });
    }

    void GlobalBindings::initBindlessDefaultResources()
    {
        auto& device = mRenderBackend.getDevice();

        rhi::TextureDesc textureDesc;
        textureDesc.type = rhi::TextureType::Texture_2D;
        textureDesc.format = rhi::TextureFormat::RGBA8_UNORM;
        textureDesc.extent = uint3(1, 1, 1);
        textureDesc.mipCount = 1;
        textureDesc.layerCount = 1;
        textureDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Transfer_Dst;

        mDefaultTexture2D = device.createTexture(textureDesc, rhi::MemoryType::Gpu_Only);

        {
            rhi::TextureViewDesc viewDesc = rhi::makeTextureDefaultViewDesc(mDefaultTexture2D->getDesc());
            viewDesc.type = rhi::TextureViewType::Texture_View_2D;
            viewDesc.range.layerCount = 1;
            mDefaultTexture2DView = mDefaultTexture2D->createView(viewDesc);
        }

        {
            rhi::TextureViewDesc viewDesc = rhi::makeTextureDefaultViewDesc(mDefaultTexture2D->getDesc());
            viewDesc.type = rhi::TextureViewType::Texture_View_2D_Array;
            mDefaultTexture2DArrayView = mDefaultTexture2D->createView(viewDesc);
        }

        textureDesc.type = rhi::TextureType::Texture_3D;
        textureDesc.layerCount = 1;
        mDefaultTexture3D = device.createTexture(textureDesc, rhi::MemoryType::Gpu_Only);

        {
            rhi::TextureViewDesc viewDesc = rhi::makeTextureDefaultViewDesc(mDefaultTexture3D->getDesc());
            viewDesc.type = rhi::TextureViewType::Texture_View_3D;
            viewDesc.range.layerCount = 1;
            mDefaultTexture3DView = mDefaultTexture3D->createView(viewDesc);
        }

        textureDesc.type = rhi::TextureType::Texture_Cube;
        textureDesc.layerCount = 6;
        mDefaultTextureCube = device.createTexture(textureDesc, rhi::MemoryType::Gpu_Only);
        {
            rhi::TextureViewDesc viewDesc = rhi::makeTextureDefaultViewDesc(mDefaultTextureCube->getDesc());
            viewDesc.type = rhi::TextureViewType::Texture_View_Cube;
            mDefaultTextureCubeView = mDefaultTextureCube->createView(viewDesc);
        }

        rhi::SamplerDesc samplerDesc;
        mDefaultSampler = device.createSampler(samplerDesc);
    }

    void GlobalBindings::initGlobalDataBindGroup(const BindlessConfig& bindlessConfig)
    {
        std::vector<rhi::BindGroupLayoutEntry> entries;

        auto& frameDataEntry = entries.emplace_back();
        frameDataEntry.binding = 0;
        frameDataEntry.type = rhi::BindingType::Uniform_Buffer;
        frameDataEntry.access = rhi::BindingAccess::Read_Only;
        frameDataEntry.count = 1;
        frameDataEntry.stageFlags = rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment | rhi::ShaderStage::Compute;

        if (mBindlessEnabled)
        {
            auto& texture2DEntry = entries.emplace_back();
            texture2DEntry.binding = kTexture2DBinding;
            texture2DEntry.type = rhi::BindingType::Sampled_Texture;
            texture2DEntry.access = rhi::BindingAccess::Read_Only;
            texture2DEntry.count = bindlessConfig.maxTexture2DCount;
            texture2DEntry.stageFlags = rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment | rhi::ShaderStage::Compute;
            texture2DEntry.bindless = true;

            auto& texture2DArrayEntry = entries.emplace_back();
            texture2DArrayEntry.binding = kTexture2DArrayBinding;
            texture2DArrayEntry.type = rhi::BindingType::Sampled_Texture;
            texture2DArrayEntry.access = rhi::BindingAccess::Read_Only;
            texture2DArrayEntry.count = bindlessConfig.maxTexture2DArrayCount;
            texture2DArrayEntry.stageFlags = rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment | rhi::ShaderStage::Compute;
            texture2DArrayEntry.bindless = true;

            auto& texture3DEntry = entries.emplace_back();
            texture3DEntry.binding = kTexture3DBinding;
            texture3DEntry.type = rhi::BindingType::Sampled_Texture;
            texture3DEntry.access = rhi::BindingAccess::Read_Only;
            texture3DEntry.count = bindlessConfig.maxTexture3DCount;
            texture3DEntry.stageFlags = rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment | rhi::ShaderStage::Compute;
            texture3DEntry.bindless = true;

            auto& textureCubeEntry = entries.emplace_back();
            textureCubeEntry.binding = kTextureCubeBinding;
            textureCubeEntry.type = rhi::BindingType::Sampled_Texture;
            textureCubeEntry.access = rhi::BindingAccess::Read_Only;
            textureCubeEntry.count = bindlessConfig.maxTextureCubeCount;
            textureCubeEntry.stageFlags = rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment | rhi::ShaderStage::Compute;
            textureCubeEntry.bindless = true;

            auto& samplerEntry = entries.emplace_back();
            samplerEntry.binding = kSamplerBinding;
            samplerEntry.type = rhi::BindingType::Sampler;
            samplerEntry.access = rhi::BindingAccess::Read_Only;
            samplerEntry.count = bindlessConfig.maxSamplerCount;
            samplerEntry.samplerBindingType = rhi::SamplerBindingType::Filtering;
            samplerEntry.stageFlags = rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment | rhi::ShaderStage::Compute;
            samplerEntry.bindless = true;
        }

        mGlobalDataBindGroupLayout = mRenderBackend.getBindGroupLayoutCache().getOrCreate(entries);
        auto& device = mRenderBackend.getDevice();
        mGlobalDataBindGroup = device.createBindGroup(*mGlobalDataBindGroupLayout);

        {
            rhi::BufferDesc bufferDesc;
            bufferDesc.size = sizeof(FrameData);
            bufferDesc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::Transfer_Dst;
            mFrameDataUniformBuffer = device.createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);
        }

        std::vector<rhi::BindGroupEntry> initialEntries;

        rhi::BufferBindingInfo frameDataInfo;
        frameDataInfo.buffer = mFrameDataUniformBuffer.get();
        frameDataInfo.offset = 0;
        frameDataInfo.range = sizeof(FrameData);

        initialEntries.push_back({ 0, rhi::BindingType::Uniform_Buffer, 0, { &frameDataInfo, 1 }, {}, {} });
        mGlobalDataBindGroup->update(initialEntries);

        if (mBindlessEnabled)
        {
            initialEntries.clear();

            std::vector<rhi::TextureView*> texture2Ds(bindlessConfig.maxTexture2DCount);
            for (uint32_t i = 0; i < bindlessConfig.maxTexture2DCount; ++i)
                texture2Ds[i] = mDefaultTexture2DView.get();
            auto& texture2DEntry = initialEntries.emplace_back();
            texture2DEntry.binding = kTexture2DBinding;
            texture2DEntry.type = rhi::BindingType::Sampled_Texture;
            texture2DEntry.index = 0;
            texture2DEntry.textures = texture2Ds;

            std::vector<rhi::TextureView*> texture2DArrays(bindlessConfig.maxTexture2DArrayCount);
            for (uint32_t i = 0; i < bindlessConfig.maxTexture2DArrayCount; ++i)
                texture2DArrays[i] = mDefaultTexture2DArrayView.get();
            auto& texture2DArrayEntry = initialEntries.emplace_back();
            texture2DArrayEntry.binding = kTexture2DArrayBinding;
            texture2DArrayEntry.type = rhi::BindingType::Sampled_Texture;
            texture2DArrayEntry.index = 0;
            texture2DArrayEntry.textures = texture2DArrays;

            std::vector<rhi::TextureView*> texture3Ds(bindlessConfig.maxTexture3DCount);
            for (uint32_t i = 0; i < bindlessConfig.maxTexture3DCount; ++i)
                texture3Ds[i] = mDefaultTexture3DView.get();
            auto& texture3DEntry = initialEntries.emplace_back();
            texture3DEntry.binding = kTexture3DBinding;
            texture3DEntry.type = rhi::BindingType::Sampled_Texture;
            texture3DEntry.index = 0;
            texture3DEntry.textures = texture3Ds;

            std::vector<rhi::TextureView*> textureCubes(bindlessConfig.maxTextureCubeCount);
            for (uint32_t i = 0; i < bindlessConfig.maxTextureCubeCount; ++i)
                textureCubes[i] = mDefaultTextureCubeView.get();
            auto& textureCubeEntry = initialEntries.emplace_back();
            textureCubeEntry.binding = kTextureCubeBinding;
            textureCubeEntry.type = rhi::BindingType::Sampled_Texture;
            textureCubeEntry.index = 0;
            textureCubeEntry.textures = textureCubes;

            std::vector<rhi::Sampler*> samplers(bindlessConfig.maxSamplerCount);
            for (uint32_t i = 0; i < bindlessConfig.maxSamplerCount; ++i)
                samplers[i] = mDefaultSampler.get();
            auto& samplerEntry = initialEntries.emplace_back();
            samplerEntry.binding = kSamplerBinding;
            samplerEntry.type = rhi::BindingType::Sampler;
            samplerEntry.index = 0;
            samplerEntry.samplers = samplers;

            mGlobalDataBindGroup->update(initialEntries);
        }

        rhi::PipelineLayoutDesc pipelineLayoutDesc;
        pipelineLayoutDesc.bindGroupLayouts[0] = mGlobalDataBindGroupLayout;
        mPipelineLayout = device.createPipelineLayout(pipelineLayoutDesc);
    }

}
