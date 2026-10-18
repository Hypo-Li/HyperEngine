#include "PipelineCache.h"
#include <Core/Common/Hash.h>

namespace hyp
{
    PipelineCache::PipelineCache(const rhi::Device& device, BindGroupLayoutCache& bindGroupLayoutCache) :
        mDevice(device),
        mBindGroupLayoutCache(bindGroupLayoutCache)
    {
    }

    bool PipelineCache::PipelineLayoutKey::operator==(const PipelineLayoutKey& other) const noexcept
    {
        for (uint32_t i = 0; i < kMaxBindGroupLayoutCount; ++i)
            if (bindGroupLayouts[i] != other.bindGroupLayouts[i])
                return false;
        return true;
    }

    TRef<rhi::PipelineLayout> PipelineCache::getOrCreateLayout(const PipelineLayoutKey& key)
    {
        auto it = mPipelineLayoutMap.find(key);
        if (it != mPipelineLayoutMap.end())
            return it->second;

        TRef<rhi::PipelineLayout> pipelineLayout = createPipelineLayout(key);
        mPipelineLayoutMap.emplace(key, pipelineLayout);
        return pipelineLayout;
    }

    bool PipelineCache::GraphicsKey::operator==(const GraphicsKey& other) const noexcept
    {
        return pipelineState == other.pipelineState &&
            pipelineLayout == other.pipelineLayout;
    }

    TRef<rhi::Pipeline> PipelineCache::getOrCreateGraphicsPipeline(const GraphicsKey& key)
    {
        auto it = mGraphicsPipelineMap.find(key);
        if (it != mGraphicsPipelineMap.end())
            return it->second;

        TRef<rhi::Pipeline> pipeline = createGraphicsPipeline(key);
        mGraphicsPipelineMap.emplace(key, pipeline);
        return pipeline;
    }

    bool PipelineCache::ComputeKey::operator==(const ComputeKey& other) const noexcept
    {
        return shader == other.shader &&
            pipelineLayout == other.pipelineLayout;
    }

    TRef<rhi::Pipeline> PipelineCache::getOrCreateComputePipeline(const ComputeKey& key)
    {
        auto it = mComputePipelineMap.find(key);
        if (it != mComputePipelineMap.end())
            return it->second;

        TRef<rhi::Pipeline> pipeline = createComputePipeline(key);
        mComputePipelineMap.emplace(key, pipeline);
        return pipeline;
    }

    TRef<rhi::PipelineLayout> PipelineCache::createPipelineLayout(const PipelineLayoutKey& key)
    {
        // 找最高的非空下标——中间(比如 set2 材质参数为空、set3 蒙皮不为空)不能留洞: 每个
        // 后端的 pipeline layout 构造都要求 [0, setLayoutCount) 范围内每一项都是合法 handle,
        // 不接受"跳过某个下标"这种表示法
        int highestNonNullIndex = -1;
        for (int i = static_cast<int>(PipelineLayoutKey::kMaxBindGroupLayoutCount) - 1; i >= 0; --i)
        {
            if (key.bindGroupLayouts[i] != nullptr)
            {
                highestNonNullIndex = i;
                break;
            }
        }

        // 内容寻址、全局共享的空 BindGroupLayout, 只用来填补最高非空下标之前的洞——不是
        // "真的需要绑定什么", 纯粹是满足"每个下标都得有合法 handle"这条约束
        TRef<rhi::BindGroupLayout> emptyLayout;

        rhi::PipelineLayoutDesc pipelineLayoutDesc;
        for (int i = 0; i <= highestNonNullIndex; ++i)
        {
            if (key.bindGroupLayouts[i] != nullptr)
            {
                pipelineLayoutDesc.bindGroupLayouts[i] = key.bindGroupLayouts[i];
            }
            else
            {
                if (!emptyLayout)
                    emptyLayout = mBindGroupLayoutCache.getOrCreate({});
                pipelineLayoutDesc.bindGroupLayouts[i] = emptyLayout.get();
            }
        }

        return mDevice.createPipelineLayout(pipelineLayoutDesc);
    }

    TRef<rhi::Pipeline> PipelineCache::createGraphicsPipeline(const GraphicsKey& key)
    {
        rhi::GraphicsPipelineDesc pipelineDesc;

        const GraphicsPipelineState& pipelineState = key.pipelineState;
        const rhi::ShaderModule* shaderModules[2] = { pipelineState.shader.vertexModule, pipelineState.shader.fragmentModule };
        pipelineDesc.shaderStages = shaderModules;

        std::vector<rhi::VertexInputBinding> bindings;
        std::vector<rhi::VertexInputAttribute> attributes;
        key.pipelineState.vertexLayout.writeVertexLayoutBindingsAndAttributes(bindings, attributes);
        pipelineDesc.vertexInputState.bindings = bindings;
        pipelineDesc.vertexInputState.attributes = attributes;

        pipelineDesc.inputAssemblyState.topology = static_cast<rhi::PrimitiveTopology>(pipelineState.topology);

        auto& rasterState = pipelineDesc.rasterizationState;
        rasterState.cullMode = static_cast<rhi::CullMode>(pipelineState.cullMode);
        rasterState.frontFace = static_cast<rhi::FrontFace>(pipelineState.frontFace);
        rasterState.depthClampEnable = pipelineState.depthClampEnable;

        pipelineDesc.multisampleState.sampleCount = static_cast<uint8_t>(1u << pipelineState.samples);

        auto& depthStencilState = pipelineDesc.depthStencilState;
        depthStencilState.depthTestEnable = pipelineState.depthTestEnable;
        depthStencilState.depthWriteEnable = pipelineState.depthWriteEnable;
        depthStencilState.depthCompareOp = static_cast<rhi::CompareOp>(pipelineState.depthCompareOp);
        depthStencilState.stencilTestEnable = pipelineState.stencilTestEnable;
        depthStencilState.front = pipelineState.stencilState->getFront();
        depthStencilState.back = pipelineState.stencilState->getBack();

        uint8_t colorAttachmentCount = 0;
        rhi::TextureFormat colorFormats[8] = { rhi::TextureFormat::Undefined };
        for (uint8_t i = 0; i < 8; ++i)
        {
            colorFormats[i] = pipelineState.attachmentFormats->getColorFormat(i);
            if (colorFormats[i] == rhi::TextureFormat::Undefined)
                break;
            ++colorAttachmentCount;
        }

        std::vector<rhi::ColorBlendAttachmentState> attachmentBlendStates(colorAttachmentCount);
        for (uint8_t i = 0; i < colorAttachmentCount; ++i)
            attachmentBlendStates[i] = pipelineState.colorBlendState->getAttachmentBlendState(i);
        pipelineDesc.colorBlendState.attachments = attachmentBlendStates;

        pipelineDesc.pipelineLayout = key.pipelineLayout;

        if (colorAttachmentCount)
            pipelineDesc.colorFormats = { colorFormats, colorAttachmentCount };
        pipelineDesc.depthStencilFormat = pipelineState.attachmentFormats->getDepthStencilFormat();

        return mDevice.createGraphicsPipeline(pipelineDesc);
    }

    TRef<rhi::Pipeline> PipelineCache::createComputePipeline(const ComputeKey& key)
    {
        rhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.shaderModule = key.shader.computeModule;
        pipelineDesc.pipelineLayout = key.pipelineLayout;
        return mDevice.createComputePipeline(pipelineDesc);
    }

    size_t PipelineCache::PipelineLayoutKeyHash::operator()(const PipelineLayoutKey& key) const noexcept
    {
        size_t seed = 0;
        for (uint8_t i = 0; i < PipelineLayoutKey::kMaxBindGroupLayoutCount; ++i)
            if (key.bindGroupLayouts[i])
                Hash::combine(seed, key.bindGroupLayouts[i]);
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

