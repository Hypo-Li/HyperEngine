#include "MaterialProxy.h"
#include "RenderBackend.h"

namespace hyp
{
    uint32_t gMaterialId = 0;
    std::vector<uint32_t> gFreeMaterialIds;

    // 临时实现, 后续需要建立统一的材质管理器
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
        std::vector<ShaderMacro> shaderMacros,
        const std::vector<Parameter>& parameters
    ) : mAttributes(attributes),
        mRenderMask(evaluateBuiltinRenderMask(attributes)),
        mMaterialShader(materialShader),
        mShaderMacros(std::move(shaderMacros))
    {
        mId = acquireMaterialId();
        resolveParameterBindings(parameters);
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
        std::vector<ShaderMacro> shaderMacros,
        const std::vector<Parameter>& parameters)
    {
        destroyParameterResources();
        mAttributes = attributes;
        mRenderMask = evaluateBuiltinRenderMask(attributes);
        mMaterialShader = materialShader;
        mShaderMacros = std::move(shaderMacros);
        resolveParameterBindings(parameters);
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
                binding.parameter = parameter;

                copyParameterBindingToUniformBufferCache(binding);
            }
        }
        if (!updatedParameters.empty())
        {
            mParametersDirty = true;
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
        case MaterialParameterType::Texture2D:
            return 8;
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
        case MaterialParameterType::Texture2D:
            return 8;
        default:
            assert(false && "Invalid Material Parameter Type");
            return 0;
        }
    }

    void MaterialProxy::prepare(vk::CommandBuffer& cmd)
    {
        if (isParametersDirty())
        {
            uploadToGPU(cmd);
        }

        // Prepare textures
        for (auto& binding : mParameterBindings)
        {
            if (isTextureParameterType(binding.parameter.type))
            {
                auto value = std::get<std::pair<TextureProxy*, SamplerProxy*>>(binding.parameter.value);
                value.first->prepare(cmd);
            }
        }
    }

    ShaderVariant MaterialProxy::getOrCreateShaderVariant(
        HString renderTag,
        VertexLayout vertexLayout
    ) noexcept
    {
        return mMaterialShader->getOrCreateVariant(renderTag, vertexLayout, mShaderMacros);
    }

    void MaterialProxy::cachePipeline(
        HString renderTag,
        VertexLayout vertexLayout,
        vk::Pipeline* pipeline
    )
    {
        PipelineLookupKey pipelineLookupKey = {};
        pipelineLookupKey.vertexLayout = vertexLayout;
        pipelineLookupKey.renderTag = renderTag;
        mCachedPipelines[pipelineLookupKey] = pipeline;
    }

    vk::Pipeline* MaterialProxy::getCachedPipeline(
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
        std::visit([&](auto&& arg)
            {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::pair<TextureProxy*, SamplerProxy*>>)
                {
                    uint32_t indices[2];
                    indices[0] = arg.first->getBindlessHandle().index;
                    indices[1] = arg.second->getBindlessHandle().index;
                    memcpy(mParametersBufferCache.data() + binding.offset, indices, binding.size);
                }
                else
                {
                    memcpy(mParametersBufferCache.data() + binding.offset, &arg, binding.size);
                }
            },
            binding.parameter.value
        );
    }

    void MaterialProxy::resolveParameterBindings(const std::vector<Parameter>& parameters)
    {
        mParameterBindings.clear();
        mNameToBindingIndex.clear();

        // We use std140 layout for uniform buffer, and use bindless.
        mParameterBindings.reserve(parameters.size());

        uint32_t currentOffset = 0;

        for (const auto& parameter : parameters)
        {
            uint32_t alignment = getStd140Alignment(parameter.type);
            uint32_t size = getParameterSize(parameter.type);

            // 将当前偏移量向上取整到 alignment 的倍数
            currentOffset = (currentOffset + alignment - 1) & ~(alignment - 1);

            mNameToBindingIndex[parameter.name] = static_cast<uint32_t>(mParameterBindings.size());
            mParameterBindings.emplace_back(parameter, currentOffset, size);

            currentOffset += size;
        }

        uint32_t uniformBufferSize = 0;
        if (!mParameterBindings.empty())
        {
            static constexpr uint32_t kMaxAlignment = 16;
            uniformBufferSize = mParameterBindings.back().offset + mParameterBindings.back().size;
            uniformBufferSize = (uniformBufferSize + kMaxAlignment - 1) & ~(kMaxAlignment - 1);
        }

        if (uniformBufferSize == 0)
            return;

        mParametersBufferCache.resize(uniformBufferSize);
        for (const auto& binding : mParameterBindings)
        {
            copyParameterBindingToUniformBufferCache(binding);
        }

        mParametersDirty = true;
    }

    void MaterialProxy::uploadToGPU(vk::CommandBuffer& cmd)
    {
        assert(mParametersDirty);

        if (!mParametersBuffer)
            return;

        // Uniform Buffer 的大小不应过大
        cmd.updateBuffer(mParametersBuffer->getHandle(), 0, mParametersBufferCache.size(), mParametersBufferCache.data());

        VkBufferMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_2_UNIFORM_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = mParametersBuffer->getHandle();
        barrier.offset = 0;
        barrier.size = mParametersBufferCache.size();
        cmd.pipelineBarrier2({ &barrier, 1 });

        mParametersDirty = false;
    }

    uint32_t MaterialProxy::evaluateBuiltinRenderMask(Attributes attributes) noexcept
    {
        uint32_t renderMask = 0u;
        auto& renderTagRegistry = getRenderBackend().getRenderTagRegistry();
        switch (attributes.domain)
        {
        case MaterialDomain::Surface:
        {
            if (attributes.blendMode == BlendMode::Opaque || attributes.blendMode == BlendMode::Masked)
            {
                renderMask |= renderTagRegistry.getRenderMask(BuiltinRenderTags::Opaque);
                renderMask |= renderTagRegistry.getRenderMask(BuiltinRenderTags::DepthOnly);
                renderMask |= renderTagRegistry.getRenderMask(BuiltinRenderTags::ShadowDepth);
            }
            else
            {
                renderMask |= renderTagRegistry.getRenderMask(BuiltinRenderTags::Translucent);
            }
            break;
        }
        default:
            break;
        }
        return renderMask;
    }

    void MaterialProxy::createParameterResources()
    {
        if (mParametersBufferCache.size())
        {
            auto& device = getRenderBackend().getDevice();
            vk::Buffer::CreateInfo bufferInfo;
            bufferInfo.size = mParametersBufferCache.size();
            bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            mParametersBuffer = new vk::Buffer(device, bufferInfo, VMA_MEMORY_USAGE_GPU_ONLY);

            VkDescriptorSetLayoutBinding bindings[1] = {};
            bindings[0].binding = 0;
            bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = VK_SHADER_STAGE_ALL;
            bindings[0].pImmutableSamplers = nullptr;
            mParametersDescriptorSet = device.getDescriptorAllocator().allocate(bindings);

            vk::DescriptorWriter writer;
            writer.writeBuffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, mParametersBuffer->getHandle(), 0, mParametersBufferCache.size());
            writer.update(*mParametersDescriptorSet);
        }
    }

    void MaterialProxy::destroyParameterResources()
    {
        if (mParametersBuffer || mParametersDescriptorSet)
        {
            getRenderBackend().getDeferredTaskQueue().push(
                [parametersBuffer = std::move(mParametersBuffer),
                descriptorSet = std::move(mParametersDescriptorSet)]() mutable
                {
                    descriptorSet = nullptr;
                    parametersBuffer = nullptr;
                },
                kMaxFramesInFlight
            );
        }
    }

}
