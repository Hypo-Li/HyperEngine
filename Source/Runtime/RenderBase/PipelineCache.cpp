#include "PipelineCache.h"
#include "Vulkan/Context.h"
#include <Core/Common/Hash.h>

namespace hyp
{
    PipelineCache::PipelineCache(const vk::Device& device) :
        mDevice(device)
    {
    }

    bool PipelineCache::PipelineLayoutKey::operator==(const PipelineLayoutKey& other) const noexcept
    {
        for (uint32_t i = 0; i < kMaxDescriptorSetLayoutCount; ++i)
            if (descriptorSetLayouts[i] != other.descriptorSetLayouts[i])
                return false;
        if (pushConstantRangeSize != other.pushConstantRangeSize)
            return false;
        return true;
    }

    vk::PipelineLayout* PipelineCache::getOrCreatePipelineLayout(const PipelineLayoutKey& key)
    {
        auto it = mPipelineLayoutMap.find(key);
        if (it != mPipelineLayoutMap.end())
            return it->second;

        vk::PipelineLayout* pipelineLayout = createPipelineLayout(key);
        mPipelineLayoutMap.emplace(key, pipelineLayout);
        return pipelineLayout;
    }

    bool PipelineCache::GraphicsKey::operator==(const GraphicsKey& other) const noexcept
    {
        return pipelineState == other.pipelineState &&
            pipelineLayout == other.pipelineLayout;
    }

    vk::Pipeline* PipelineCache::getOrCreateGraphicsPipeline(const GraphicsKey& key)
    {
        auto it = mGraphicsPipelineMap.find(key);
        if (it != mGraphicsPipelineMap.end())
            return it->second;

        vk::Pipeline* pipeline = createGraphicsPipeline(key);
        mGraphicsPipelineMap.emplace(key, pipeline);
        return pipeline;
    }

    bool PipelineCache::ComputeKey::operator==(const ComputeKey& other) const noexcept
    {
        return shader == other.shader &&
            pipelineLayout == other.pipelineLayout;
    }

    vk::Pipeline* PipelineCache::getOrCreateComputePipeline(const ComputeKey& key)
    {
        auto it = mComputePipelineMap.find(key);
        if (it != mComputePipelineMap.end())
            return it->second;

        vk::Pipeline* pipeline = createComputePipeline(key);
        mComputePipelineMap.emplace(key, pipeline);
        return pipeline;
    }

    vk::PipelineLayout* PipelineCache::createPipelineLayout(const PipelineLayoutKey& key)
    {
        uint32_t descriptorSetLayoutCount = 0;
        for (size_t i = 0; i < PipelineLayoutKey::kMaxDescriptorSetLayoutCount; ++i)
        {
            if (key.descriptorSetLayouts[i] == VK_NULL_HANDLE)
                break;
            ++descriptorSetLayoutCount;
        }

        std::span<const VkDescriptorSetLayout> descriptorSetLayouts(key.descriptorSetLayouts, descriptorSetLayoutCount);

        VkPushConstantRange pushConstantRange;
        pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL;
        pushConstantRange.offset = 0;
        pushConstantRange.size = key.pushConstantRangeSize;
        std::span<const VkPushConstantRange> pc = {};
        if (key.pushConstantRangeSize)
        {
            pc = { &pushConstantRange, 1 };
        }

        vk::PipelineLayout* result = new vk::PipelineLayout(mDevice, descriptorSetLayouts, pc);
        return result;
    }

    vk::Pipeline* PipelineCache::createGraphicsPipeline(const GraphicsKey& key)
    {
        vk::Pipeline::GraphicsCreateInfo pipelineInfo;

        const GraphicsPipelineState& pipelineState = key.pipelineState;

        vk::Pipeline::ShaderStage shaderStages[2];
        shaderStages[0] = { VK_SHADER_STAGE_VERTEX_BIT, pipelineState.shader.vertexModule->getHandle(), "main" };
        shaderStages[1] = { VK_SHADER_STAGE_FRAGMENT_BIT, pipelineState.shader.fragmentModule->getHandle(), "main" };
        pipelineInfo.shaderStages = shaderStages;

        std::vector<VkVertexInputBindingDescription> bindings;
        std::vector<VkVertexInputAttributeDescription> attributes;
        key.pipelineState.vertexLayout.writeVertexLayoutBindingsAndAttributes(bindings, attributes);
        pipelineInfo.vertexInputState.bindings = bindings;
        pipelineInfo.vertexInputState.attributes = attributes;

        pipelineInfo.inputAssemblyState.topology = static_cast<VkPrimitiveTopology>(pipelineState.topology);

        VkViewport viewports[] = { {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f} };
        VkRect2D scissors[] = { { {0, 0}, {1, 1} } };
        pipelineInfo.viewportState.viewports = viewports;
        pipelineInfo.viewportState.scissors = scissors;

        auto& rasterState = pipelineInfo.rasterizationState;
        rasterState.polygonMode = static_cast<VkPolygonMode>(pipelineState.polygonMode);
        rasterState.cullMode = static_cast<VkCullModeFlags>(pipelineState.cullMode);
        rasterState.frontFace = static_cast<VkFrontFace>(pipelineState.frontFace);

        pipelineInfo.multisampleState.rasterizationSamples = static_cast<VkSampleCountFlagBits>(1u << pipelineState.samples);

        auto& depthStencilState = pipelineInfo.depthStencilState;
        depthStencilState.depthTestEnable = pipelineState.depthTestEnable;
        depthStencilState.depthWriteEnable = pipelineState.depthWriteEnable;
        depthStencilState.depthCompareOp = static_cast<VkCompareOp>(pipelineState.depthCompareOp);
        depthStencilState.stencilTestEnable = pipelineState.stencilTestEnable;
        depthStencilState.front = pipelineState.stencilState->getFront();
        depthStencilState.back = pipelineState.stencilState->getBack();

        uint8_t colorAttachmentCount = 0;
        VkFormat colorFormats[8] = { VK_FORMAT_UNDEFINED };
        for (uint8_t i = 0; i < 8; ++i)
        {
            colorFormats[i] = pipelineState.attachmentFormats->getColorFormat(i);
            if (colorFormats[i] == VK_FORMAT_UNDEFINED)
                break;
            ++colorAttachmentCount;
        }

        std::vector<VkPipelineColorBlendAttachmentState> attachmentBlendStates(colorAttachmentCount);
        for (uint8_t i = 0; i < colorAttachmentCount; ++i)
            attachmentBlendStates[i] = pipelineState.colorBlendState->getAttachmentBlendState(i);
        pipelineInfo.colorBlendState.attachments = attachmentBlendStates;

        pipelineInfo.layout = key.pipelineLayout;

        if (colorAttachmentCount)
            pipelineInfo.colorFormats = { colorFormats, colorAttachmentCount };
        pipelineInfo.depthStencilFormat = pipelineState.attachmentFormats->getDepthStencilFormat();

        VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            // TODO: only for debug or editor
            // VK_DYNAMIC_STATE_POLYGON_MODE_EXT
        };
        pipelineInfo.dynamicState.dynamicStates = dynamicStates;

        return new vk::Pipeline(mDevice, pipelineInfo);
    }

    vk::Pipeline* PipelineCache::createComputePipeline(const ComputeKey& key)
    {
        vk::Pipeline::ComputeCreateInfo pipelineInfo;
        pipelineInfo.shaderStage = { VK_SHADER_STAGE_COMPUTE_BIT, key.shader.computeModule->getHandle(), "main" };
        pipelineInfo.layout = key.pipelineLayout;
        return new vk::Pipeline(mDevice, pipelineInfo);
    }

    size_t PipelineCache::PipelineLayoutKeyHash::operator()(const PipelineLayoutKey& key) const noexcept
    {
        size_t seed = 0;
        for (uint8_t i = 0; i < PipelineLayoutKey::kMaxDescriptorSetLayoutCount; ++i)
            Hash::combine(seed, key.descriptorSetLayouts[i]);
        Hash::combine(seed, key.pushConstantRangeSize);
        return seed;
    }

    size_t PipelineCache::GraphicsKeyHash::operator()(const GraphicsKey& key) const noexcept
    {
        size_t seed = 0;
        Hash::combine(seed, key.pipelineState);
        Hash::combine(seed, key.pipelineLayout);
        return seed;
    }

    size_t PipelineCache::ComputeKeyHash::operator()(const ComputeKey& key) const noexcept
    {
        size_t seed = 0;
        Hash::combine(seed, key.shader);
        Hash::combine(seed, key.pipelineLayout);
        return seed;
    }

}

