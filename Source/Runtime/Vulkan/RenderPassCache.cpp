#include "RenderPassCache.h"

#include <Core/Common/LogSystem.h>
#include <vector>

namespace hyp::vk
{
    static VkAttachmentLoadOp convertLoadOp(rhi::AttachmentLoadOp op) noexcept
    {
        switch (op)
        {
        case rhi::AttachmentLoadOp::Load:      return VK_ATTACHMENT_LOAD_OP_LOAD;
        case rhi::AttachmentLoadOp::Clear:     return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case rhi::AttachmentLoadOp::Dont_Care: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        default:                               return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        }
    }

    static VkAttachmentStoreOp convertStoreOp(rhi::AttachmentStoreOp op) noexcept
    {
        switch (op)
        {
        case rhi::AttachmentStoreOp::Store:     return VK_ATTACHMENT_STORE_OP_STORE;
        case rhi::AttachmentStoreOp::Dont_Care: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        default:                                return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }
    }

    static VkImageLayout getAttachmentLayout(VkImageAspectFlags aspect) noexcept
    {
        if (aspect == VK_IMAGE_ASPECT_COLOR_BIT)
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    RenderPassCache::RenderPassCache(const Device& device) :
        mDevice(device)
    {
    }

    RenderPassCache::~RenderPassCache()
    {
        for (auto& [key, renderPass] : mCache)
        {
            vkDestroyRenderPass(mDevice.getHandle(), renderPass, nullptr);
        }
        mCache.clear();
    }

    VkRenderPass RenderPassCache::getOrCreateRenderPass(const RenderPassKey& key)
    {
        auto it = mCache.find(key);
        if (it != mCache.end())
        {
            return it->second;
        }

        const uint32_t maxColorAttachments = 8;
        const bool hasDepthStencil = key.depthStencilFormat != VK_FORMAT_UNDEFINED;

        uint32_t attachmentCount = key.colorAttachmentCount + (hasDepthStencil ? 1 : 0);
        std::vector<VkAttachmentDescription> attachments(attachmentCount);
        std::vector<VkAttachmentReference> colorReferences(key.colorAttachmentCount);

        VkAttachmentReference depthStencilReference = {};
        depthStencilReference.attachment = VK_ATTACHMENT_UNUSED;

        for (uint32_t i = 0; i < key.colorAttachmentCount; ++i)
        {
            VkAttachmentDescription& desc = attachments[i];
            desc.format = key.colorFormats[i];
            desc.samples = key.sampleCount;
            desc.loadOp = key.colorLoadOps[i];
            desc.storeOp = key.colorStoreOps[i];
            desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            desc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            desc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            colorReferences[i].attachment = i;
            colorReferences[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        if (hasDepthStencil)
        {
            uint32_t dsIndex = key.colorAttachmentCount;
            VkAttachmentDescription& desc = attachments[dsIndex];
            desc.format = key.depthStencilFormat;
            desc.samples = key.sampleCount;
            desc.loadOp = key.depthLoadOp;
            desc.storeOp = key.depthStoreOp;
            desc.stencilLoadOp = key.stencilLoadOp;
            desc.stencilStoreOp = key.stencilStoreOp;
            desc.initialLayout = key.depthStencilImageLayout;
            desc.finalLayout = key.depthStencilImageLayout;

            depthStencilReference.attachment = dsIndex;
            depthStencilReference.layout = key.depthStencilImageLayout;
        }

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = key.colorAttachmentCount;
        subpass.pColorAttachments = colorReferences.data();
        subpass.pDepthStencilAttachment = hasDepthStencil ? &depthStencilReference : nullptr;

        VkSubpassDependency dependencies[2] = {};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = 0;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = 0;

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = attachmentCount;
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 2;
        renderPassInfo.pDependencies = dependencies;

        VkRenderPass renderPass = VK_NULL_HANDLE;
        if (vkCreateRenderPass(mDevice.getHandle(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create Vulkan render pass");
        }

        mCache.emplace(key, renderPass);
        return renderPass;
    }

    VkRenderPass RenderPassCache::getOrCreateRenderPass(
        std::span<const VkFormat> colorFormats,
        VkFormat depthStencilFormat,
        VkFormat depthFormat,
        VkFormat stencilFormat,
        VkSampleCountFlagBits sampleCount
    )
    {
        RenderPassKey key;
        key.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
        for (uint32_t i = 0; i < key.colorAttachmentCount; ++i)
        {
            key.colorFormats[i] = colorFormats[i];
            key.colorLoadOps[i] = VK_ATTACHMENT_LOAD_OP_LOAD;
            key.colorStoreOps[i] = VK_ATTACHMENT_STORE_OP_STORE;
        }
        key.depthStencilFormat = depthStencilFormat;
        key.depthLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        key.depthStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        key.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        key.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        key.sampleCount = sampleCount;
        return getOrCreateRenderPass(key);
    }

    VkRenderPass RenderPassCache::getOrCreateRenderPass(
        std::span<const VkFormat> colorFormats,
        std::span<const VkAttachmentLoadOp> colorLoadOps,
        std::span<const VkAttachmentStoreOp> colorStoreOps,
        VkFormat depthStencilFormat,
        VkAttachmentLoadOp depthLoadOp,
        VkAttachmentStoreOp depthStoreOp,
        VkAttachmentLoadOp stencilLoadOp,
        VkAttachmentStoreOp stencilStoreOp,
        VkImageLayout depthStencilImageLayout,
        VkSampleCountFlagBits sampleCount
    )
    {
        RenderPassKey key;
        key.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
        for (uint32_t i = 0; i < key.colorAttachmentCount; ++i)
        {
            key.colorFormats[i] = colorFormats[i];
            key.colorLoadOps[i] = i < colorLoadOps.size() ? colorLoadOps[i] : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            key.colorStoreOps[i] = i < colorStoreOps.size() ? colorStoreOps[i] : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }
        key.depthStencilFormat = depthStencilFormat;
        key.depthLoadOp = depthLoadOp;
        key.depthStoreOp = depthStoreOp;
        key.stencilLoadOp = stencilLoadOp;
        key.stencilStoreOp = stencilStoreOp;
        key.depthStencilImageLayout = depthStencilImageLayout;
        key.sampleCount = sampleCount;
        return getOrCreateRenderPass(key);
    }

    FramebufferCache::FramebufferCache(const Device& device) :
        mDevice(device)
    {
    }

    FramebufferCache::~FramebufferCache()
    {
        for (auto& [key, framebuffer] : mCache)
        {
            vkDestroyFramebuffer(mDevice.getHandle(), framebuffer, nullptr);
        }
        mCache.clear();
    }

    VkFramebuffer FramebufferCache::getOrCreateFramebuffer(
        VkRenderPass renderPass,
        std::span<const VkImageView> attachments,
        uint32_t width,
        uint32_t height,
        uint32_t layers
    )
    {
        FramebufferKey key;
        key.renderPass = renderPass;
        key.width = width;
        key.height = height;
        key.layers = layers;
        key.attachmentCount = static_cast<uint32_t>(attachments.size());
        for (uint32_t i = 0; i < key.attachmentCount; ++i)
        {
            key.attachments[i] = attachments[i];
        }

        auto it = mCache.find(key);
        if (it != mCache.end())
        {
            return it->second;
        }

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = key.attachmentCount;
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = width;
        framebufferInfo.height = height;
        framebufferInfo.layers = layers;

        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        if (vkCreateFramebuffer(mDevice.getHandle(), &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create Vulkan framebuffer");
        }

        mCache.emplace(key, framebuffer);
        return framebuffer;
    }
}
