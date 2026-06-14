#pragma once
#include "Device.h"

namespace hyp::vk
{
    class Framebuffer : public Refable
    {
    public:
        Framebuffer(
            const Device& device,
            VkRenderPass renderPass,
            std::span<const VkImageView> attachments,
            uint32_t width,
            uint32_t height,
            uint32_t layers = 1
        );

        ~Framebuffer() noexcept;

        VkFramebuffer getHandle() const noexcept
        {
            return mFramebuffer;
        }

    private:
        const Device& mDevice;
        VkFramebuffer mFramebuffer{ VK_NULL_HANDLE };
    };
}
