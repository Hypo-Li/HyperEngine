#include "TransientResources.h"

namespace hyp::rg
{
    TransientResources::TransientResources(const vk::Device& device) :
        mDevice(device)
    {
    }

    TransientResources::~TransientResources()
    {
    }

    vk::Buffer* TransientResources::acquireBuffer(const BufferKey& key)
    {
        TRef<vk::Buffer> result;
        auto it = mBufferMap.find(key);
        if (it == mBufferMap.end())
        {
            result = createBuffer(mDevice, key);
            mBufferMap.emplace(key, result);
        }
        else
        {
            result = it->second.resource;
        }
        return result;
    }

    vk::ImageView* TransientResources::acquireTexture(const TextureKey& key)
    {
        TRef<vk::ImageView> result;
        auto it = mTextureMap.find(key);
        if (it == mTextureMap.end())
        {
            auto [image, imageView] = createTexture(mDevice, key);
            mSubTextureMaps.emplace(image, SubTextureMap{});
            result = imageView;
            mTextureMap.emplace(key, result);
        }
        else
        {
            result = it->second.resource;
        }
        return result;
    }

    vk::ImageView* TransientResources::acquireSubTexture(vk::ImageView* texture, const SubTextureKey& key)
    {
        vk::Image* image = &texture->getImage();
        SubTextureMap& subTextureMap = mSubTextureMaps[image];
        auto subTextureIt = subTextureMap.find(key);
        if (subTextureIt != subTextureMap.end())
        {
            return subTextureIt->second.resource;
        }
        vk::ImageView* result = createSubTexture(image, key);
        subTextureMap.emplace(key, result);
        return result;
    }

    vk::RenderPass* TransientResources::acquireRenderPass(const RenderPassKey& key)
    {
        auto findResult = mRenderPassMap.find(key);
        if (findResult != mRenderPassMap.end())
            return findResult->second.resource;
        vk::RenderPass* renderPass = createRenderPass(mDevice, key);
        mRenderPassMap.emplace(key, renderPass);
        return renderPass;
    }

    vk::Framebuffer* TransientResources::acquireFramebuffer(const FramebufferKey& key)
    {
        auto findResult = mFramebufferMap.find(key);
        if (findResult != mFramebufferMap.end())
            return findResult->second.resource;
        vk::Framebuffer* framebuffer = createFramebuffer(mDevice, key);
        mFramebufferMap.emplace(key, framebuffer);
        return framebuffer;
    }

    void TransientResources::destroyUselessResources()
    {
        for (auto it = mBufferMap.begin(); it != mBufferMap.end(); )
        {
            auto& record = it->second;
            if (record.shouldDestroy())
            {
                it = mBufferMap.erase(it);
            }
            else
            {
                ++it;
            }
        }

        for (auto it = mTextureMap.begin(); it != mTextureMap.end(); )
        {
            auto& record = it->second;
            if (record.shouldDestroy())
            {
                // 销毁未使用的primary image view
                vk::ImageView* imageView = record.resource;
                imageView->getImage().destroyView(*imageView);
                it = mTextureMap.erase(it);
            }
            else
            {
                ++it;
            }
        }

        for (auto imageIt = mSubTextureMaps.begin(); imageIt != mSubTextureMaps.end();)
        {
            auto& subTextureMap = imageIt->second;
            for (auto it = subTextureMap.begin(); it != subTextureMap.end();)
            {
                auto& record = it->second;
                if (record.shouldDestroy())
                {
                    vk::ImageView* imageView = record.resource;
                    imageView->getImage().destroyView(*imageView);
                    it = subTextureMap.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            if (subTextureMap.empty() && imageIt->first->getViews().size() == 0)
                imageIt = mSubTextureMaps.erase(imageIt);
            else
                ++imageIt;
        }

        for (auto it = mRenderPassMap.begin(); it != mRenderPassMap.end();)
        {
            if (it->second.shouldDestroy())
                it = mRenderPassMap.erase(it);
            else
                ++it;
        }

        for (auto it = mFramebufferMap.begin(); it != mFramebufferMap.end();)
        {
            if (it->second.shouldDestroy())
                it = mFramebufferMap.erase(it);
            else
                ++it;
        }
    }

    void TransientResources::clearBarriers()
    {
        mPassPrevBufferBarriers.clear();
        mPassPrevImageBarriers.clear();
        mPassPostBufferBarriers.clear();
        mPassPostImageBarriers.clear();
    }

    vk::Buffer* TransientResources::createBuffer(const vk::Device& device, const BufferKey& key)
    {
        vk::Buffer::CreateInfo createInfo;
        createInfo.size = key.description.size;
        createInfo.usage = key.description.usage;
        vk::Buffer* buffer = new vk::Buffer(device, createInfo, VMA_MEMORY_USAGE_AUTO);
        return buffer;
    }

    std::pair<vk::Image*, vk::ImageView*> TransientResources::createTexture(const vk::Device& device, const TextureKey& key)
    {
        vk::Image::CreateInfo createInfo;
        createInfo.imageType = key.description.imageType;
        createInfo.format = key.description.format;
        createInfo.extent = key.description.extent;
        createInfo.mipLevels = key.description.mipLevels;
        createInfo.arrayLayers = key.description.arrayLayers;
        createInfo.samples = static_cast<VkSampleCountFlagBits>(key.description.samples);
        createInfo.usage = key.description.usage;
        createInfo.isCube = key.description.isCube;
        vk::Image* image = new vk::Image(device, createInfo, VMA_MEMORY_USAGE_AUTO);
        vk::Image::ViewInfo imageViewInfo(*image);
        return { image, image->createView(imageViewInfo) };
    }

    vk::ImageView* TransientResources::createSubTexture(vk::Image* image, const SubTextureKey& key)
    {
        vk::Image::ViewInfo imageViewInfo(*image);
        if (image->isCube() && !key.description.isCube)
            imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        imageViewInfo.subresourceRange.aspectMask = key.description.aspectMask;
        imageViewInfo.subresourceRange.baseMipLevel = key.description.baseMipLevel;
        imageViewInfo.subresourceRange.levelCount = key.description.levelCount;
        imageViewInfo.subresourceRange.baseArrayLayer = key.description.baseArrayLayer;
        imageViewInfo.subresourceRange.layerCount = key.description.layerCount;
        return image->createView(imageViewInfo);
    }

    vk::RenderPass* TransientResources::createRenderPass(const vk::Device& device, const RenderPassKey& key)
    {
        VkAttachmentDescription attachments[kMaxColorAttachmentCount + 1] = {};
        uint32_t colorAttachmentCount = 0;

        const bool isPresentPass = (key.flags & RenderPassFlags::Present) != RenderPassFlags::None;

        for (uint32_t i = 0; i < kMaxColorAttachmentCount; ++i)
        {
            if (key.colorAttachments[i].format == AttachmentFormat::Undefined)
            {
                colorAttachmentCount = i;
                break;
            }

            VkAttachmentDescription& attachment = attachments[i];
            attachment.format = convertToVkFormat(key.colorAttachments[i].format);
            attachment.samples = static_cast<VkSampleCountFlagBits>(key.samples);
            attachment.loadOp = convertToVkAttachmentLoadOp(key.colorAttachments[i].loadOp);
            attachment.storeOp = convertToVkAttachmentStoreOp(key.colorAttachments[i].storeOp);
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            VkImageLayout imageLayout = convertToVkImageLayout(key.colorAttachments[i].layout);
            attachment.initialLayout = isPresentPass ? VK_IMAGE_LAYOUT_UNDEFINED : imageLayout;
            attachment.finalLayout = isPresentPass ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : imageLayout;
        }

        VkAttachmentReference colorAttachmentRefs[kMaxColorAttachmentCount] = {};

        for (uint32_t i = 0, attachmentIndex = 0; i < colorAttachmentCount; ++i, ++attachmentIndex)
        {
            colorAttachmentRefs[i].attachment = attachmentIndex;
            colorAttachmentRefs[i].layout = convertToVkImageLayout(key.colorAttachments[attachmentIndex].layout);
        }

        VkAttachmentReference depthStencilAttachmentRef;
        bool hasDepthStencilAttachment = key.depthStencilAttachment.format != AttachmentFormat::Undefined;
        if (hasDepthStencilAttachment)
        {
            VkAttachmentDescription& depthStencilAttachment = attachments[colorAttachmentCount];
            depthStencilAttachment.format = convertToVkFormat(key.depthStencilAttachment.format);
            depthStencilAttachment.samples = static_cast<VkSampleCountFlagBits>(key.samples);
            depthStencilAttachment.loadOp = convertToVkAttachmentLoadOp(key.depthStencilAttachment.loadOp);
            depthStencilAttachment.storeOp = convertToVkAttachmentStoreOp(key.depthStencilAttachment.storeOp);
            depthStencilAttachment.stencilLoadOp = convertToVkAttachmentLoadOp(key.stencilLoadOp);
            depthStencilAttachment.stencilStoreOp = convertToVkAttachmentStoreOp(key.stencilStoreOp);
            depthStencilAttachment.initialLayout = convertToVkImageLayout(key.depthStencilAttachment.layout);
            depthStencilAttachment.finalLayout = convertToVkImageLayout(key.depthStencilAttachment.layout);

            depthStencilAttachmentRef.attachment = colorAttachmentCount;
            depthStencilAttachmentRef.layout = convertToVkImageLayout(key.depthStencilAttachment.layout);
        }
        uint32_t attachmentCount = hasDepthStencilAttachment ? colorAttachmentCount + 1 : colorAttachmentCount;
        assert(attachmentCount > 0);

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = colorAttachmentCount;
        subpass.pColorAttachments = colorAttachmentCount ? colorAttachmentRefs : nullptr;
        subpass.inputAttachmentCount = 0;
        subpass.pInputAttachments = nullptr;
        subpass.pDepthStencilAttachment = hasDepthStencilAttachment ? &depthStencilAttachmentRef : nullptr;

        vk::RenderPass* renderPass = new vk::RenderPass(device, { attachments, attachmentCount }, { &subpass, 1 }, {});
        return renderPass;
    }

    vk::Framebuffer* TransientResources::createFramebuffer(const vk::Device& device, const FramebufferKey& key)
    {
        uint8_t attachmentCount = 0;
        VkImageView attachments[kMaxColorAttachmentCount + 1];

        for (int i = 0; i < kMaxColorAttachmentCount; ++i)
        {
            if (key.colors[i] != VK_NULL_HANDLE)
            {
                attachments[attachmentCount] = key.colors[i];
                ++attachmentCount;
            }
            else
            {
                break;
            }
        }
        if (key.depthStencil != VK_NULL_HANDLE)
        {
            attachments[attachmentCount] = key.depthStencil;
            ++attachmentCount;
        }

        vk::Framebuffer* framebuffer = new vk::Framebuffer(
            device,
            key.renderPass->getHandle(),
            std::span<const VkImageView>(attachments, attachmentCount),
            key.width,
            key.height,
            key.layers
        );
        return framebuffer;
    }
}

