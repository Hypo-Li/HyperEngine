#pragma once
#include "VolkInclude.h"

#include "Features.h"
#include "Memory.h"

#include <RHI/Device.h>
#include <RHI/Context.h>

namespace hyp::vk
{
    class DescriptorAllocator;
    class RenderPassCache;
    class FramebufferCache;
    class PipelineCache;

    struct DeviceDispatch
    {
        PFN_vkCmdBeginRenderingKHR cmdBeginRendering = nullptr;
        PFN_vkCmdEndRenderingKHR cmdEndRendering = nullptr;
        PFN_vkCmdPipelineBarrier2KHR cmdPipelineBarrier2 = nullptr;
        PFN_vkGetSemaphoreCounterValueKHR getSemaphoreCounterValue = nullptr;
        PFN_vkWaitSemaphoresKHR waitSemaphores = nullptr;
        PFN_vkSignalSemaphoreKHR signalSemaphore = nullptr;
    };

    class Device : public rhi::Device
    {
    public:
        Device(
            const rhi::ContextDesc& desc,
            VkInstance instance,
            VkPhysicalDevice physicalDevice,
            uint32_t instanceApiVersion,
            bool physicalDeviceProperties2
        );

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

        VkInstance getInstance() const noexcept
        {
            return mInstance;
        }

        VkPhysicalDevice getPhysicalDevice() const noexcept
        {
            return mPhysicalDevice;
        }

        VkDevice getHandle() const noexcept
        {
            return mDevice;
        }

        uint32_t getApiVersion() const noexcept
        {
            return mApiVersion;
        }

        const DeviceDispatch& getDispatch() const noexcept
        {
            return mDispatch;
        }

        struct QueueInfo
        {
            VkQueue queue = VK_NULL_HANDLE;
            uint32_t familyIndex = 0;
            uint32_t queueIndex = 0;
        };

        QueueInfo getGraphicsQueueInfo() const noexcept
        {
            return mGraphicsQueueInfo;
        }

        QueueInfo getComputeQueueInfo() const noexcept
        {
            return mComputeQueueInfo;
        }

        QueueInfo getTransferQueueInfo() const noexcept
        {
            return mTransferQueueInfo;
        }

        VmaAllocator getAllocator() const noexcept
        {
            return mAllocator;
        }

        DescriptorAllocator& getDescriptorAllocator() const noexcept
        {
            return *mDescriptorAllocator;
        }

        const FeatureSupport& getVkFeatures() const noexcept
        {
            return mVkFeatures;
        }

        RenderPassCache& getRenderPassCache() const noexcept
        {
            return *mRenderPassCache;
        }

        FramebufferCache& getFramebufferCache() const noexcept
        {
            return *mFramebufferCache;
        }

        PipelineCache& getPipelineCache() const noexcept
        {
            return *mPipelineCache;
        }

    protected:
        TRef<rhi::Buffer> createBufferImpl(const rhi::BufferDesc& desc, rhi::MemoryType memoryType) const override;

        TRef<rhi::Texture> createTextureImpl(const rhi::TextureDesc& desc, rhi::MemoryType memoryType) const override;

        TRef<rhi::Sampler> createSamplerImpl(const rhi::SamplerDesc& desc) const override;

    private:
        VkInstance mInstance = VK_NULL_HANDLE;
        VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;

        VkDevice mDevice = VK_NULL_HANDLE;
        uint32_t mApiVersion = VK_API_VERSION_1_0;
        DeviceDispatch mDispatch;
        QueueInfo mGraphicsQueueInfo;
        VkCommandPool mGraphicsCommandPool = VK_NULL_HANDLE;
        QueueInfo mComputeQueueInfo;
        VkCommandPool mComputeCommandPool = VK_NULL_HANDLE;
        QueueInfo mTransferQueueInfo;
        VkCommandPool mTransferCommandPool = VK_NULL_HANDLE;
        VmaAllocator mAllocator = VK_NULL_HANDLE;

        std::unique_ptr<DescriptorAllocator> mDescriptorAllocator;
        FeatureSupport mVkFeatures;

        std::unique_ptr<RenderPassCache> mRenderPassCache;
        std::unique_ptr<FramebufferCache> mFramebufferCache;
        std::unique_ptr<PipelineCache> mPipelineCache;
    };
}
