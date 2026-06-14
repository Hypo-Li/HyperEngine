#pragma once
#include "Device.h"

namespace hyp::vk
{
    class RenderPass : public Refable
    {
    public:
        RenderPass(
            const Device& device,
            std::span<const VkAttachmentDescription> attachments,
            std::span<const VkSubpassDescription> subpasses,
            std::span<const VkSubpassDependency> dependencies
        ) noexcept;
        ~RenderPass() noexcept;

        VkRenderPass getHandle() const noexcept
        {
            return mRenderPass;
        }

    private:
        const Device& mDevice;
        VkRenderPass mRenderPass;
    };
}
