#pragma once
#include "Buffer.h"
#include "Texture.h"
#include "Sampler.h"
#include "BindGroup.h"
#include "ShaderModule.h"
#include "Pipeline.h"
#include "CommandList.h"
#include "Fence.h"
#include "Swapchain.h"
#include "Features.h"

namespace hyp::rhi
{
    enum class SubmitFlags : uint32_t
    {
        None = 0,
        Wait_Swapchain_Image = 1 << 0,  // 等待 swapchain image 可用
        Signal_Present_Ready = 1 << 1,  // 标记 swapchain image 可提交 present
    };
    ENABLE_ENUM_CLASS_BITWISE(SubmitFlags)

    class Context;
    class Device
    {
    public:
        Device() = default;

        virtual ~Device() = default;

        Swapchain* getSwapchain() const noexcept
        {
            return mSwapchain.get();
        }

        const FeatureSupport& getFeatures() const noexcept
        {
            return mFeatures;
        }

        virtual void waitIdle() const = 0;

        virtual TRef<Buffer> createBuffer(const BufferDesc& desc, MemoryType memoryType) const = 0;

        virtual TRef<Texture> createTexture(const TextureDesc& desc, MemoryType memoryType) const = 0;

        virtual TRef<Sampler> createSampler(const SamplerDesc& desc) const = 0;

        virtual TRef<BindGroupLayout> createBindGroupLayout(std::span<const BindGroupLayoutEntry> entries) const = 0;

        virtual TRef<BindGroup> createBindGroup(const BindGroupLayout& layout) const = 0;

        virtual TRef<ShaderModule> createShaderModule(ShaderStage stage, std::vector<uint8_t>&& code, ShaderCodeFormat codeFormat) const = 0;

        virtual TRef<PipelineLayout> createPipelineLayout(const PipelineLayoutDesc& desc) const = 0;

        virtual TRef<Pipeline> createGraphicsPipeline(const GraphicsPipelineDesc& desc) const = 0;

        virtual TRef<Pipeline> createComputePipeline(const ComputePipelineDesc& desc) const = 0;

        virtual TRef<Fence> createFence() const = 0;

        virtual TRef<CommandList> createCommandList(QueueType queueType) const = 0;

        virtual void submit(
            CommandList* commandList,
            Fence* waitFence,
            uint64_t waitValue,
            Fence* signalFence,
            uint64_t signalValue,
            SubmitFlags flags = SubmitFlags::None
        ) const = 0;

    protected:
        std::unique_ptr<Swapchain> mSwapchain;
        FeatureSupport mFeatures;
    };
}
