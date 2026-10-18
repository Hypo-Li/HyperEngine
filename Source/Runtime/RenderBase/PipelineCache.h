#pragma once
#include "PipelineState.h"
#include "BindGroupLayoutCache.h"
#include <RHI/Pipeline.h>
#include <RHI/Device.h>

namespace hyp
{
    class PipelineCache
    {
    public:
        PipelineCache(const rhi::Device& device, BindGroupLayoutCache& bindGroupLayoutCache);

        struct PipelineLayoutKey
        {
            static constexpr size_t kMaxBindGroupLayoutCount = 4;
            const rhi::BindGroupLayout* bindGroupLayouts[kMaxBindGroupLayoutCount];

            bool operator==(const PipelineLayoutKey& other) const noexcept;
        };

        TRef<rhi::PipelineLayout> getOrCreateLayout(const PipelineLayoutKey& key);

        struct GraphicsKey
        {
            GraphicsPipelineState pipelineState;
            const rhi::PipelineLayout* pipelineLayout;

            bool operator==(const GraphicsKey& other) const noexcept;
        };

        TRef<rhi::Pipeline> getOrCreateGraphicsPipeline(const GraphicsKey& key);

        struct ComputeKey
        {
            ShaderVariant shader;
            const rhi::PipelineLayout* pipelineLayout;

            bool operator==(const ComputeKey& other) const noexcept;
        };

        TRef<rhi::Pipeline> getOrCreateComputePipeline(const ComputeKey& key);

    private:
        TRef<rhi::PipelineLayout> createPipelineLayout(const PipelineLayoutKey& key);

        TRef<rhi::Pipeline> createGraphicsPipeline(const GraphicsKey& key);

        TRef<rhi::Pipeline> createComputePipeline(const ComputeKey& key);

    private:
        const rhi::Device& mDevice;
        BindGroupLayoutCache& mBindGroupLayoutCache;
        struct PipelineLayoutKeyHash
        {
            size_t operator()(const PipelineLayoutKey& key) const noexcept;
        };
        std::unordered_map<PipelineLayoutKey, TRef<rhi::PipelineLayout>, PipelineLayoutKeyHash> mPipelineLayoutMap;
        struct GraphicsKeyHash
        {
            size_t operator()(const GraphicsKey& key) const noexcept;
        };
        std::unordered_map<GraphicsKey, TRef<rhi::Pipeline>, GraphicsKeyHash> mGraphicsPipelineMap;
        struct ComputeKeyHash
        {
            size_t operator()(const ComputeKey& key) const noexcept;
        };
        std::unordered_map<ComputeKey, TRef<rhi::Pipeline>, ComputeKeyHash> mComputePipelineMap;
    };
}
