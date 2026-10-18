#include "Texture.h"
#include "VkFormatUtil.h"

namespace hyp::vk
{
    static VkImageViewType convertToVkImageViewType(rhi::TextureViewType type)
    {
        switch (type)
        {
        case rhi::TextureViewType::Texture_View_2D:
            return VK_IMAGE_VIEW_TYPE_2D;
        case rhi::TextureViewType::Texture_View_2D_Array:
            return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case rhi::TextureViewType::Texture_View_3D:
            return VK_IMAGE_VIEW_TYPE_3D;
        case rhi::TextureViewType::Texture_View_Cube:
            return VK_IMAGE_VIEW_TYPE_CUBE;
        default:
            return VK_IMAGE_VIEW_TYPE_2D;
        }
    }

    static VkImageType convertToVkImageType(rhi::TextureType type)
    {
        switch (type)
        {
        case rhi::TextureType::Texture_2D:
            return VK_IMAGE_TYPE_2D;
        case rhi::TextureType::Texture_3D:
            return VK_IMAGE_TYPE_3D;
        case rhi::TextureType::Texture_Cube:
        case rhi::TextureType::Texture_2D_Array:
            return VK_IMAGE_TYPE_2D;
        default:
            return VK_IMAGE_TYPE_2D;
        }
    }

    static VkImageUsageFlags convertToVkImageUsageFlags(rhi::TextureUsage usage)
    {
        VkImageUsageFlags result = 0;
        if (enumHasAnyFlags(usage, rhi::TextureUsage::Transfer_Src))
            result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (enumHasAnyFlags(usage, rhi::TextureUsage::Transfer_Dst))
            result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (enumHasAnyFlags(usage, rhi::TextureUsage::Sampled))
            result |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if (enumHasAnyFlags(usage, rhi::TextureUsage::Storage))
            result |= VK_IMAGE_USAGE_STORAGE_BIT;
        if (enumHasAnyFlags(usage, rhi::TextureUsage::Color_Attachment))
            result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (enumHasAnyFlags(usage, rhi::TextureUsage::Depth_Stencil_Attachment))
            result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        return result;
    }

    TextureView::TextureView(Texture& texture, const rhi::TextureViewDesc& desc) :
        rhi::TextureView(texture, desc)
    {
        VkImageViewCreateInfo imageViewInfo = {};
        imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewInfo.image = static_cast<const Texture&>(texture).getHandle();
        imageViewInfo.viewType = convertToVkImageViewType(desc.type);
        imageViewInfo.format = convertToVkFormat(desc.format);
        imageViewInfo.components = {
            VkComponentSwizzle::VK_COMPONENT_SWIZZLE_IDENTITY,
            VkComponentSwizzle::VK_COMPONENT_SWIZZLE_IDENTITY,
            VkComponentSwizzle::VK_COMPONENT_SWIZZLE_IDENTITY,
            VkComponentSwizzle::VK_COMPONENT_SWIZZLE_IDENTITY
        };
        imageViewInfo.subresourceRange = {
            convertToVkImageAspectFlags(desc.range.aspect),
            desc.range.baseMip,
            desc.range.mipCount,
            desc.range.baseLayer,
            desc.range.layerCount
        };

        vkCreateImageView(texture.getVkDevice().getHandle(), &imageViewInfo, nullptr, &mImageView);
    }

    TextureView::~TextureView()
    {
        vkDestroyImageView(getVkTexture().getVkDevice().getHandle(), mImageView, nullptr);
    }

    void TextureView::setDebugName(HString name)
    {
        rhi::TextureView::setDebugName(name);

        if (vkSetDebugUtilsObjectNameEXT)
        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = {};
            nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            nameInfo.objectType = VK_OBJECT_TYPE_IMAGE_VIEW;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(mImageView);
            nameInfo.pObjectName = getDebugName().toCString();
            vkSetDebugUtilsObjectNameEXT(getVkTexture().getVkDevice().getHandle(), &nameInfo);
        }
    }

    const Texture& TextureView::getVkTexture() const noexcept
    {
        return static_cast<const Texture&>(getTexture());
    }

    Texture::Texture(const Device& device, const rhi::TextureDesc& desc, rhi::MemoryType memoryType) :
        rhi::Texture(device, desc, memoryType)
    {
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = convertToVkImageType(desc.type);
        imageInfo.format = convertToVkFormat(desc.format);
        imageInfo.extent = { desc.extent.x, desc.extent.y, desc.extent.z };
        imageInfo.mipLevels = desc.mipCount;
        imageInfo.arrayLayers = desc.layerCount;
        imageInfo.samples = static_cast<VkSampleCountFlagBits>(desc.samples);
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = convertToVkImageUsageFlags(desc.usage);
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (desc.type == rhi::TextureType::Texture_Cube)
            imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

        VmaAllocationCreateInfo allocationInfo = makeVmaAllocationCreateInfo(memoryType);
        vmaCreateImage(device.getAllocator(), &imageInfo, &allocationInfo, &mImage, &mAllocation, nullptr);
    }

    Texture::Texture(const Device& device, const rhi::TextureDesc& desc, rhi::MemoryType memoryType, VkImage image) :
        rhi::Texture(device, desc, memoryType),
        mImage(image)
    {
    }

    Texture::~Texture()
    {
        if (mAllocation == VK_NULL_HANDLE)
            return;

        auto& device = getVkDevice();
        vkDestroyImage(device.getHandle(), mImage, nullptr);
        vmaFreeMemory(device.getAllocator(), mAllocation);
    }

    void Texture::setDebugName(HString name)
    {
        rhi::Texture::setDebugName(name);

        if (vkSetDebugUtilsObjectNameEXT)
        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = {};
            nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(mImage);
            nameInfo.pObjectName = getDebugName().toCString();
            vkSetDebugUtilsObjectNameEXT(getVkDevice().getHandle(), &nameInfo);
        }
    }

    TRef<rhi::TextureView> Texture::createViewImpl(const rhi::TextureViewDesc& desc)
    {
        incViewCount();
        return new TextureView(*this, desc);
    }

}
