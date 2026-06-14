#include "DummyRenderPassCache.h"

namespace hyp::vk
{
    DummyRenderPassCache::DummyRenderPassCache(const Device& device)
        : mDevice(device)
    {
    }

    RenderPass* DummyRenderPassCache::getOrCreateDummyRenderPass(std::span<const VkFormat> colorFormats, VkFormat depthStencilFormat, VkSampleCountFlagBits samples)
    {
        Key key;
        key.colorFormats.assign(colorFormats.begin(), colorFormats.end());
        key.depthStencilFormat = depthStencilFormat;
        key.samples = samples;

        auto it = mDummyRenderPassMap.find(key);
        if (it != mDummyRenderPassMap.end())
            return it->second;

        bool hasDepthStencilFormat = depthStencilFormat != VK_FORMAT_UNDEFINED;
        uint32_t attachmentCount = static_cast<uint32_t>(colorFormats.size()) + (hasDepthStencilFormat ? 1 : 0);
        std::vector<VkAttachmentDescription> attachments;
        attachments.reserve(attachmentCount);
        for (uint32_t i = 0; i < colorFormats.size(); ++i)
        {
            VkAttachmentDescription& attachment = attachments.emplace_back();
            attachment.format = colorFormats[i];
            attachment.samples = samples;
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
        }

        if (hasDepthStencilFormat)
        {
            VkAttachmentDescription& attachment = attachments.emplace_back();
            attachment.format = depthStencilFormat;
            attachment.samples = samples;
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
        }

        std::vector<VkAttachmentReference> colorAttachmentRefs(colorFormats.size());
        for (uint32_t i = 0; i < colorFormats.size(); ++i)
        {
            colorAttachmentRefs[i].attachment = i;
            colorAttachmentRefs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        VkAttachmentReference depthStencilAttachmentRefs;
        if (hasDepthStencilFormat)
        {
            depthStencilAttachmentRefs.attachment = static_cast<uint32_t>(colorFormats.size());
            depthStencilAttachmentRefs.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }

        VkSubpassDescription subpasses[1] = {};
        VkSubpassDescription& subpass = subpasses[0];
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<uint32_t>(colorAttachmentRefs.size());
        subpass.pColorAttachments = colorAttachmentRefs.data();
        subpass.pDepthStencilAttachment = hasDepthStencilFormat ? &depthStencilAttachmentRefs : nullptr;

        RenderPass* dummyRenderPass = new RenderPass(mDevice, attachments, subpasses, {});
        mDummyRenderPassMap.emplace(key, dummyRenderPass);

        return dummyRenderPass;
    }

    bool DummyRenderPassCache::Key::operator==(const Key& other) const
    {
        return colorFormats == other.colorFormats &&
            depthStencilFormat == other.depthStencilFormat &&
            samples == other.samples;
    }

    size_t DummyRenderPassCache::KeyHash::operator()(const Key& key) const
    {
        size_t h = std::hash<VkFormat>()(key.depthStencilFormat);
        h ^= std::hash<uint32_t>()(key.samples) + 0x9e3779b9 + (h << 6) + (h >> 2);

        for (auto fmt : key.colorFormats)
            h ^= std::hash<VkFormat>()(fmt) + 0x9e3779b9 + (h << 6) + (h >> 2);
        
        return h;
    }

}
