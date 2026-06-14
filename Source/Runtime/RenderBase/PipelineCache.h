#pragma once
#include "PipelineState.h"
#include "Vulkan/Pipeline.h"

namespace hyp
{
    class PipelineCache
    {
    public:
        PipelineCache(const vk::Device& device);

        struct PipelineLayoutKey
        {
            static constexpr size_t kMaxDescriptorSetLayoutCount = 4;
            VkDescriptorSetLayout descriptorSetLayouts[kMaxDescriptorSetLayoutCount];
            uint32_t pushConstantRangeSize = 0;

            bool operator==(const PipelineLayoutKey& other) const noexcept;
        };

        vk::PipelineLayout* getOrCreatePipelineLayout(const PipelineLayoutKey& key);

        struct GraphicsKey
        {
            GraphicsPipelineState pipelineState;
            vk::PipelineLayout* pipelineLayout;

            bool operator==(const GraphicsKey& other) const noexcept;
        };

        vk::Pipeline* getOrCreateGraphicsPipeline(const GraphicsKey& key);

        struct ComputeKey
        {
            ShaderVariant shader;
            vk::PipelineLayout* pipelineLayout;

            bool operator==(const ComputeKey& other) const noexcept;
        };

        vk::Pipeline* getOrCreateComputePipeline(const ComputeKey& key);

    private:
        vk::PipelineLayout* createPipelineLayout(const PipelineLayoutKey& key);

        vk::Pipeline* createGraphicsPipeline(const GraphicsKey& key);

        vk::Pipeline* createComputePipeline(const ComputeKey& key);

    private:
        const vk::Device& mDevice;
        struct PipelineLayoutKeyHash
        {
            size_t operator()(const PipelineLayoutKey& key) const noexcept;
        };
        std::unordered_map<PipelineLayoutKey, TRef<vk::PipelineLayout>, PipelineLayoutKeyHash> mPipelineLayoutMap;
        struct GraphicsKeyHash
        {
            size_t operator()(const GraphicsKey& key) const noexcept;
        };
        std::unordered_map<GraphicsKey, TRef<vk::Pipeline>, GraphicsKeyHash> mGraphicsPipelineMap;
        struct ComputeKeyHash
        {
            size_t operator()(const ComputeKey& key) const noexcept;
        };
        std::unordered_map<ComputeKey, TRef<vk::Pipeline>, ComputeKeyHash> mComputePipelineMap;
    };
}
