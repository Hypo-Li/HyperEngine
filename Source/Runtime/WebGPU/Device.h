#pragma once
#include <dawn/webgpu.h>

#include <RHI/Device.h>
#include <RHI/Context.h>

#include "Features.h"

namespace hyp::wgpu
{
    class Device : public rhi::Device
    {
    public:
        Device(const rhi::ContextDesc& desc, WGPUInstance instance, WGPUAdapter adapter);

        ~Device();

        void waitIdle() const override;

        TRef<rhi::BindGroupLayout> createBindGroupLayout(std::span<const rhi::BindGroupLayoutEntry> entries) const override;

        TRef<rhi::BindGroup> createBindGroup(const rhi::BindGroupLayout& layout) const override;

        TRef<rhi::ShaderModule> createShaderModule(rhi::ShaderStage stage, std::vector<uint8_t>&& code, rhi::ShaderCodeFormat codeFormat) const override;

        TRef<rhi::PipelineLayout> createPipelineLayout(const rhi::PipelineLayoutDesc& desc) const override;

        TRef<rhi::Pipeline> createGraphicsPipeline(const rhi::GraphicsPipelineDesc& desc) const override;

        TRef<rhi::Pipeline> createComputePipeline(const rhi::ComputePipelineDesc& desc) const override;

        TRef<rhi::Fence> createFence() const override;

        TRef<rhi::CommandList> createCommandList(rhi::QueueType queueType) const override;

        void submit(
            rhi::CommandList* commandList,
            rhi::Fence* waitFence,
            uint64_t waitValue,
            rhi::Fence* signalFence,
            uint64_t signalValue,
            rhi::SubmitFlags flags
        ) const override;

        WGPUInstance getInstance() const noexcept
        {
            return mInstance;
        }

        WGPUAdapter getAdapter() const noexcept
        {
            return mAdapter;
        }

        WGPUDevice getHandle() const noexcept
        {
            return mDevice;
        }

        WGPUQueue getQueue() const noexcept
        {
            return mQueue;
        }

        const FeatureSupport& getWgpuFeatures() const noexcept
        {
            return mWgpuFeatures;
        }

    protected:
        TRef<rhi::Buffer> createBufferImpl(const rhi::BufferDesc& desc, rhi::MemoryType memoryType) const override;

        TRef<rhi::Texture> createTextureImpl(const rhi::TextureDesc& desc, rhi::MemoryType memoryType) const override;

        TRef<rhi::Sampler> createSamplerImpl(const rhi::SamplerDesc& desc) const override;

    private:
        WGPUInstance mInstance;
        WGPUAdapter mAdapter;
        WGPUDevice mDevice;
        WGPUQueue mQueue;
        FeatureSupport mWgpuFeatures;
    };
}
