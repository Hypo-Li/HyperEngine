#pragma once
#include "Device.h"

#include <RHI/Pipeline.h>

#include <volk.h>

namespace hyp::vk
{
    class PipelineLayout : public rhi::PipelineLayout
    {
    public:
        PipelineLayout(const Device& device, const rhi::PipelineLayoutDesc& desc);
        ~PipelineLayout() override;

        const Device& getVkDevice() const noexcept
        {
            return static_cast<const Device&>(getDevice());
        }

        VkPipelineLayout getHandle() const noexcept
        {
            return mPipelineLayout;
        }

    private:
        VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
    };

    class Pipeline : public rhi::Pipeline
    {
    public:
        Pipeline(const Device& device, const rhi::GraphicsPipelineDesc& desc);
        Pipeline(const Device& device, const rhi::ComputePipelineDesc& desc);
        ~Pipeline() override;

        const Device& getVkDevice() const noexcept
        {
            return static_cast<const Device&>(getDevice());
        }

        VkPipeline getHandle() const noexcept
        {
            return mPipeline;
        }

        VkPipelineBindPoint getBindPoint() const noexcept;

    private:
        VkPipeline mPipeline = VK_NULL_HANDLE;
    };
}
