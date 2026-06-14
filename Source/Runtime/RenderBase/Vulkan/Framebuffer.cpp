#include "Framebuffer.h"

namespace hyp::vk
{
    Framebuffer::Framebuffer(
        const Device& device,
        VkRenderPass renderPass,
        std::span<const VkImageView> attachments,
        uint32_t width,
        uint32_t height,
        uint32_t layers
    ) : mDevice(device)
    {
        VkFramebufferCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createInfo.renderPass = renderPass;
        createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        createInfo.pAttachments = attachments.data();
        createInfo.width = width;
        createInfo.height = height;
        createInfo.layers = layers;

        vkCreateFramebuffer(mDevice.getHandle(), &createInfo, nullptr, &mFramebuffer);
    }

    Framebuffer::~Framebuffer() noexcept
    {
        vkDestroyFramebuffer(mDevice.getHandle(), mFramebuffer, nullptr);
    }

}
