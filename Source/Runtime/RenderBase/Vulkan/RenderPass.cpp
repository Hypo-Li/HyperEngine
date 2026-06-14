#include "RenderPass.h"

namespace hyp::vk
{
    RenderPass::RenderPass(
        const Device& device,
        std::span<const VkAttachmentDescription> attachments,
        std::span<const VkSubpassDescription> subpasses,
        std::span<const VkSubpassDependency> dependencies
    ) noexcept
        : mDevice(device)
    {
        VkRenderPassCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        createInfo.pAttachments = attachments.data();
        createInfo.subpassCount = static_cast<uint32_t>(subpasses.size());
        createInfo.pSubpasses = subpasses.data();
        createInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        createInfo.pDependencies = dependencies.data();

        vkCreateRenderPass(mDevice.getHandle(), &createInfo, nullptr, &mRenderPass);
    }

    RenderPass::~RenderPass() noexcept
    {
        vkDestroyRenderPass(mDevice.getHandle(), mRenderPass, nullptr);
    }
}
