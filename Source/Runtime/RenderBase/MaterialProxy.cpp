#include "MaterialProxy.h"
#include "RenderBackend.h"

namespace hyp
{
    uint32_t gMaterialId = 0;
    std::vector<uint32_t> gFreeMaterialIds;

    uint32_t acquireMaterialId()
    {
        if (gFreeMaterialIds.empty())
        {
            return gMaterialId++;
        }
        else
        {
            auto id = gFreeMaterialIds.back();
            gFreeMaterialIds.pop_back();
            return id;
        }
    }

    void releaseMaterialId(uint32_t id)
    {
        gFreeMaterialIds.push_back(id);
    }

    MaterialProxy::MaterialProxy(
        Attributes attributes,
        MaterialShader* materialShader,
        const std::vector<Parameter>& parameters
    ) : mAttributes(attributes),
        mRenderMask(evaluateBuiltinRenderMask(attributes)),
        mMaterialShader(materialShader),
        mShaderMacros(makeShaderMacros(attributes)),
        mBindlessEnabled(getRenderBackend().getDevice().getFeatures().bindless)
    {
        mId = acquireMaterialId();
        buildParameterBindings(parameters);
        createParameterResources();
    }

    MaterialProxy::~MaterialProxy() noexcept
    {
        destroyParameterResources();
        releaseMaterialId(mId);
    }

    void MaterialProxy::update(
        Attributes attributes,
        MaterialShader* materialShader,
        const std::vector<Parameter>& parameters
    )
    {
        destroyParameterResources();
        mAttributes = attributes;
        mRenderMask = evaluateBuiltinRenderMask(attributes);
        mMaterialShader = materialShader;
        mShaderMacros = makeShaderMacros(attributes);
        buildParameterBindings(parameters);
        createParameterResources();

        mCachedPipelines.clear();
    }

    void MaterialProxy::updateParameters(std::span<const Parameter> updatedParameters)
    {
        for (auto& parameter : updatedParameters)
        {
            auto it = mNameToBindingIndex.find(parameter.name);
            if (it != mNameToBindingIndex.end())
            {
                auto& binding = mParameterBindings[it->second];

                assert(parameter.type == binding.parameter.type);
                auto oldParameterValue = binding.parameter.value;
                binding.parameter = parameter;

                if (isTextureParameterType(binding.parameter.type) && !mBindlessEnabled)
                {
                    auto oldValue = std::get<std::pair<TextureProxy*, SamplerProxy*>>(oldParameterValue);
                    auto newValue = std::get<std::pair<TextureProxy*, SamplerProxy*>>(parameter.value);
                    if (oldValue.first != newValue.first || oldValue.second != newValue.second)
                    {
                        mTextureParametersDirty = true;
                    }
                }
                else
                {
                    copyParameterBindingToUniformBufferCache(binding);
                    mParametersDirty = true;
                }
            }
        }
    }

    static uint32_t getStd140Alignment(MaterialParameterType type) noexcept
    {
        switch (type)
        {
        case MaterialParameterType::Bool:
        case MaterialParameterType::Int:
        case MaterialParameterType::Float:
            return 4;
        case MaterialParameterType::Float2:
            return 8;
        case MaterialParameterType::Float3:
            return 16;
        case MaterialParameterType::Float4:
            return 16;
        case MaterialParameterType::Texture_2D:
        case MaterialParameterType::Texture_2D_Array:
        case MaterialParameterType::Texture_3D:
        case MaterialParameterType::Texture_Cube:
            return 4;
        default:
            assert(false && "Invalid Material Parameter Type");
            return 0;
        }
    }

    static uint32_t getParameterSize(MaterialParameterType type) noexcept
    {
        switch (type)
        {
        case MaterialParameterType::Bool:
        case MaterialParameterType::Int:
        case MaterialParameterType::Float:
            return 4;
        case MaterialParameterType::Float2:
            return 8;
        case MaterialParameterType::Float3:
            return 12;
        case MaterialParameterType::Float4:
            return 16;
        case MaterialParameterType::Texture_2D:
        case MaterialParameterType::Texture_2D_Array:
        case MaterialParameterType::Texture_3D:
        case MaterialParameterType::Texture_Cube:
            return 8;
        default:
            assert(false && "Invalid Material Parameter Type");
            return 0;
        }
    }

    void MaterialProxy::prepare(rhi::CommandList& cmdList)
    {
        if (isParametersDirty())
        {
            uploadToGPU(cmdList);
        }

        for (auto& binding : mParameterBindings)
        {
            if (isTextureParameterType(binding.parameter.type))
            {
                auto value = std::get<std::pair<TextureProxy*, SamplerProxy*>>(binding.parameter.value);
                value.first->prepare(cmdList);
            }
        }
    }

    ShaderVariant MaterialProxy::getOrCreateShaderVariant(
        HString renderTag,
        VertexLayout vertexLayout
    ) noexcept
    {
        if (usesSharedGeometryShader(renderTag))
        {
            auto& renderBackend = getRenderBackend();
            MaterialShader* sharedShader = renderBackend.getMaterialShaderManager().getOrCreateSharedGeometryShader(renderTag);
            return sharedShader->getOrCreateVariant(renderTag, vertexLayout, {});
        }
        return mMaterialShader->getOrCreateVariant(renderTag, vertexLayout, mShaderMacros);
    }

    bool MaterialProxy::usesSharedGeometryShader(HString renderTag) const noexcept
    {
        return getRenderBackend().getRenderTagRegistry().hasFlags(renderTag, RenderTagFlags::Geometry_Only)
            && mAttributes.blendMode == BlendMode::Opaque
            && !mAttributes.hasVertexPositionOffset;
    }

    void MaterialProxy::cachePipeline(
        HString renderTag,
        VertexLayout vertexLayout,
        rhi::Pipeline* pipeline
    )
    {
        PipelineLookupKey pipelineLookupKey = {};
        pipelineLookupKey.vertexLayout = vertexLayout;
        pipelineLookupKey.renderTag = renderTag;
        mCachedPipelines[pipelineLookupKey] = pipeline;
    }

    rhi::Pipeline* MaterialProxy::getCachedPipeline(
        HString renderTag,
        VertexLayout vertexLayout
    ) const noexcept
    {
        PipelineLookupKey pipelineLookupKey = {};
        pipelineLookupKey.vertexLayout = vertexLayout;
        pipelineLookupKey.renderTag = renderTag;
        auto it = mCachedPipelines.find(pipelineLookupKey);
        if (it != mCachedPipelines.end())
            return it->second;
        return nullptr;
    }

    void MaterialProxy::copyParameterBindingToUniformBufferCache(const ParameterBinding& binding)
    {
        if (binding.size == 0)
            return;

        std::visit([&](auto&& arg)
            {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::pair<TextureProxy*, SamplerProxy*>>)
                {
                    if (mBindlessEnabled)
                    {
                        uint32_t indices[2];
                        indices[0] = arg.first->getBindlessHandle().index;
                        indices[1] = arg.second->getBindlessHandle().index;
                        memcpy(mParametersBufferCache.data() + binding.offset, indices, binding.size);
                    }
                }
                else
                {
                    memcpy(mParametersBufferCache.data() + binding.offset, &arg, binding.size);
                }
            },
            binding.parameter.value
        );
    }

    void MaterialProxy::buildParameterBindings(const std::vector<Parameter>& parameters)
    {
        mParameterBindings.clear();
        mNameToBindingIndex.clear();

        mParameterBindings.reserve(parameters.size());

        uint32_t currentOffset = 0;
        uint32_t textureIndex = 0;

        bool hasTextureParameters = false;
        for (const auto& parameter : parameters)
        {
            const bool isTexture = isTextureParameterType(parameter.type);
            if (isTexture && !mBindlessEnabled)
            {
                uint32_t bindingSlot = 1 + 2 * textureIndex;
                ++textureIndex;

                mNameToBindingIndex[parameter.name] = static_cast<uint32_t>(mParameterBindings.size());
                mParameterBindings.emplace_back(parameter, 0u, 0u, bindingSlot);

                hasTextureParameters = true;
            }
            else
            {
                uint32_t alignment = getStd140Alignment(parameter.type);
                uint32_t size = getParameterSize(parameter.type);

                currentOffset = (currentOffset + alignment - 1) & ~(alignment - 1);

                mNameToBindingIndex[parameter.name] = static_cast<uint32_t>(mParameterBindings.size());
                mParameterBindings.emplace_back(parameter, currentOffset, size, 0u);

                currentOffset += size;
            }
        }

        uint32_t uniformBufferSize = 0;
        for (const auto& binding : mParameterBindings)
        {
            if (binding.size > 0)
                uniformBufferSize = std::max(uniformBufferSize, binding.offset + binding.size);
        }
        if (uniformBufferSize > 0)
        {
            static constexpr uint32_t kMaxAlignment = 16;
            uniformBufferSize = (uniformBufferSize + kMaxAlignment - 1) & ~(kMaxAlignment - 1);
        }

        mParametersBufferCache.resize(uniformBufferSize);
        for (const auto& binding : mParameterBindings)
        {
            copyParameterBindingToUniformBufferCache(binding);
        }

        mParametersDirty = true;
        if (!mBindlessEnabled && hasTextureParameters)
        {
            mTextureParametersDirty = true;
        }
    }

    void MaterialProxy::uploadToGPU(rhi::CommandList& cmdList)
    {
        assert(mParametersDirty || mTextureParametersDirty);

        if (mParametersBufferCache.empty() && !mParametersBindGroup)
        {
            mParametersDirty = false;
            mTextureParametersDirty = false;
            return;
        }

        if (mParametersBuffer && mParametersDirty)
        {
            rhi::BufferBarrier preBarrier;
            preBarrier.buffer = mParametersBuffer;
            preBarrier.offset = 0;
            preBarrier.size = mParametersBufferCache.size();
            preBarrier.oldState = rhi::ResourceState::Shader_Uniform;
            preBarrier.newState = rhi::ResourceState::Transfer_Dst;
            cmdList.insertBarriers({ &preBarrier, 1 });

            auto allocation = getRenderBackend().getStagingAllocator().forceAllocate(mParametersBufferCache.size());
            std::memcpy(allocation.mappedData, mParametersBufferCache.data(), mParametersBufferCache.size());
            rhi::BufferCopyRegion region;
            region.srcOffset = allocation.offset;
            region.dstOffset = 0;
            region.size = allocation.size;
            cmdList.copyBuffer(allocation.buffer, mParametersBuffer, { &region, 1 });

            rhi::BufferBarrier postBarrier;
            postBarrier.buffer = mParametersBuffer;
            postBarrier.offset = 0;
            postBarrier.size = allocation.size;
            postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
            postBarrier.newState = rhi::ResourceState::Shader_Uniform;
            cmdList.insertBarriers({ &postBarrier, 1 });

            mParametersDirty = false;
        }

        if (!mBindlessEnabled && mParametersBindGroup && mTextureParametersDirty)
        {
            // TODO: 该情况下, 重建ParametersBindGroup
            getRenderBackend().getDeferredTaskQueue().push(
                [parametersBindGroup = std::move(mParametersBindGroup)]() mutable
                {
                    parametersBindGroup = nullptr;
                },
                kMaxFramesInFlight
            );

            mParametersBindGroup = getRenderBackend().getDevice().createBindGroup(*mParametersBindGroupLayout);

            rhi::BufferBindingInfo bufferBinding;
            std::vector<rhi::TextureView*> textureBindings;
            std::vector<rhi::Sampler*> samplerBindings;
            std::vector<rhi::BindGroupEntry> bindingEntries;
            if (mParametersBuffer)
            {
                bufferBinding = { mParametersBuffer, 0, mParametersBufferCache.size() };
                bindingEntries.emplace_back(
                    0u,
                    rhi::BindingType::Uniform_Buffer,
                    0u,
                    std::span<const rhi::BufferBindingInfo>{ &bufferBinding, 1 },
                    std::span<const rhi::TextureView* const>{},
                    std::span<const rhi::Sampler* const>{}
                );
            }
            uint32_t textureIndex = 0;
            for (const auto& binding : mParameterBindings)
            {
                if (isTextureParameterType(binding.parameter.type))
                {
                    auto value = std::get<std::pair<TextureProxy*, SamplerProxy*>>(binding.parameter.value);
                    textureBindings.push_back(&value.first->getTextureView());
                    samplerBindings.push_back(&value.second->getSampler());
                    bindingEntries.emplace_back(
                        binding.bindingSlot,
                        rhi::BindingType::Sampled_Texture,
                        0u,
                        std::span<const rhi::BufferBindingInfo>{},
                        std::span<const rhi::TextureView* const>{ &textureBindings[textureIndex], 1 },
                        std::span<const rhi::Sampler* const>{}
                    );
                    bindingEntries.emplace_back(
                        binding.bindingSlot + 1,
                        rhi::BindingType::Sampler,
                        0u,
                        std::span<const rhi::BufferBindingInfo>{},
                        std::span<const rhi::TextureView* const>{},
                        std::span<const rhi::Sampler* const>{ &samplerBindings[textureIndex], 1 }
                    );
                    textureIndex++;
                }
            }
            mParametersBindGroup->update(bindingEntries);
            mTextureParametersDirty = false;
        }
    }

    uint32_t MaterialProxy::evaluateBuiltinRenderMask(Attributes attributes) noexcept
    {
        return getRenderBackend().getRenderTagRegistry().getApplicableRenderMask(attributes.domain, attributes.blendMode);
    }

    std::vector<ShaderMacro> MaterialProxy::makeShaderMacros(Attributes attributes) const
    {
        std::vector<ShaderMacro> shaderMacros;
        shaderMacros.reserve(6);
        shaderMacros.emplace_back("MATERIAL_DOMAIN"_hs, HString(std::to_string(uint32_t(attributes.domain))));
        shaderMacros.emplace_back("BLEND_MODE"_hs, HString(std::to_string(uint32_t(attributes.blendMode))));
        shaderMacros.emplace_back("ALPHA_CUTOFF"_hs, HString(std::to_string(attributes.alphaCutoff)));
        shaderMacros.emplace_back("SHADING_MODEL"_hs, HString(std::to_string(uint32_t(attributes.shadingModel))));
        if (attributes.doubleSided)
            shaderMacros.emplace_back("DOUBLE_SIDED"_hs, HString());
        if (attributes.tangentSpaceNormal)
            shaderMacros.emplace_back("TANGENT_SPACE_NORMAL"_hs, HString());
        return shaderMacros;
    }

    void MaterialProxy::createParameterResources()
    {
        if (mParameterBindings.empty() || !mMaterialShader)
            return;

        auto& device = getRenderBackend().getDevice();

        if (!mParametersBufferCache.empty())
        {
            rhi::BufferDesc bufferDesc;
            bufferDesc.size = mParametersBufferCache.size();
            bufferDesc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::Transfer_Dst;
            mParametersBuffer = device.createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);
        }

        std::vector<rhi::BindGroupLayoutEntry> layoutEntries;
        if (mParametersBuffer)
        {
            auto& uniformEntry = layoutEntries.emplace_back();
            uniformEntry.binding = 0;
            uniformEntry.type = rhi::BindingType::Uniform_Buffer;
            uniformEntry.access = rhi::BindingAccess::Read_Only;
            uniformEntry.count = 1;
            uniformEntry.stageFlags = rhi::ShaderStage::All;
        }

        uint32_t textureParameterCount = 0;
        if (!mBindlessEnabled)
        {
            for (const auto& binding : mParameterBindings)
            {
                if (isTextureParameterType(binding.parameter.type))
                {
                    auto [texture, sampler] = std::get<std::pair<TextureProxy*, SamplerProxy*>>(binding.parameter.value);

                    auto& textureEntry = layoutEntries.emplace_back();
                    textureEntry.binding = binding.bindingSlot;
                    textureEntry.type = rhi::BindingType::Sampled_Texture;
                    textureEntry.access = rhi::BindingAccess::Read_Only;
                    textureEntry.count = 1;
                    textureEntry.textureViewType = texture->getTextureView().getDesc().type;
                    textureEntry.textureSampleType = rhi::TextureSampleType::Float;
                    textureEntry.stageFlags = rhi::ShaderStage::All;

                    auto& samplerEntry = layoutEntries.emplace_back();
                    samplerEntry.binding = binding.bindingSlot + 1;
                    samplerEntry.type = rhi::BindingType::Sampler;
                    samplerEntry.access = rhi::BindingAccess::Read_Only;
                    samplerEntry.count = 1;
                    samplerEntry.samplerBindingType = rhi::SamplerBindingType::Filtering;
                    samplerEntry.stageFlags = rhi::ShaderStage::All;

                    textureParameterCount++;
                }
            }
        }

        if (layoutEntries.empty())
            return;

        mParametersBindGroupLayout = getRenderBackend().getBindGroupLayoutCache().getOrCreate(layoutEntries);
        mParametersBindGroup = device.createBindGroup(*mParametersBindGroupLayout);

        rhi::BufferBindingInfo bufferBinding;
        std::vector<rhi::TextureView*> textureBindings;
        std::vector<rhi::Sampler*> samplerBindings;
        if (!mBindlessEnabled)
        {
            textureBindings.reserve(textureParameterCount);
            samplerBindings.reserve(textureParameterCount);
        }

        std::vector<rhi::BindGroupEntry> bindingEntries;
        if (mParametersBuffer)
        {
            bufferBinding = { mParametersBuffer, 0, mParametersBufferCache.size() };
            auto& entry = bindingEntries.emplace_back();
            entry.binding = 0u;
            entry.type = rhi::BindingType::Uniform_Buffer;
            entry.buffers = { &bufferBinding, 1 };
        }

        uint32_t textureIndex = 0;
        for (const auto& binding : mParameterBindings)
        {
            if (isTextureParameterType(binding.parameter.type))
            {
                auto value = std::get<std::pair<TextureProxy*, SamplerProxy*>>(binding.parameter.value);
                if (!mBindlessEnabled)
                {
                    textureBindings.push_back(&value.first->getTextureView());
                    samplerBindings.push_back(&value.second->getSampler());
                    auto& textureEntry = bindingEntries.emplace_back();
                    textureEntry.binding = binding.bindingSlot;
                    textureEntry.type = rhi::BindingType::Sampled_Texture;
                    textureEntry.textures = { &textureBindings[textureIndex], 1 };

                    auto& samplerEntry = bindingEntries.emplace_back();
                    samplerEntry.binding = binding.bindingSlot + 1;
                    samplerEntry.type = rhi::BindingType::Sampler;
                    samplerEntry.samplers = { &samplerBindings[textureIndex], 1 };
                    textureIndex++;
                }
            }
        }

        mParametersBindGroup->update(bindingEntries);

        mTextureParametersDirty = false;
    }

    void MaterialProxy::destroyParameterResources()
    {
        if (mParametersBuffer || mParametersBindGroup)
        {
            getRenderBackend().getDeferredTaskQueue().push(
                [parametersBuffer = std::move(mParametersBuffer),
                bindGroupLayout = std::move(mParametersBindGroupLayout),
                bindGroup = std::move(mParametersBindGroup)]() mutable
                {
                    bindGroup = nullptr;
                    bindGroupLayout = nullptr;
                    parametersBuffer = nullptr;
                },
                kMaxFramesInFlight
            );
        }
    }

}
