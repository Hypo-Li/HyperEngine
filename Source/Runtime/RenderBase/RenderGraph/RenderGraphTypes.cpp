#include "RenderGraphTypes.h"

#include <Core/Common/Hash.h>

namespace hyp::rg
{
    bool BufferKey::operator==(const BufferKey& other) const noexcept
    {
        if (description.size == other.description.size &&
            description.usage == other.description.usage &&
            name == other.name)
        {
            return true;
        }
        return false;
    }

    size_t BufferKeyHash::operator()(const BufferKey& key) const noexcept
    {
        size_t seed = 0;
        Hash::combine(seed, key.description.size);
        Hash::combine(seed, key.description.usage);
        Hash::combine(seed, key.name);
        return seed;
    }

    bool TextureKey::operator==(const TextureKey& other) const noexcept
    {
        if (description.imageType == other.description.imageType &&
            description.format == other.description.format &&
            description.extent.width == other.description.extent.width &&
            description.extent.height == other.description.extent.height &&
            description.extent.depth == other.description.extent.depth &&
            description.mipLevels == other.description.mipLevels &&
            description.arrayLayers == other.description.arrayLayers &&
            description.samples == other.description.samples &&
            description.usage == other.description.usage &&
            name == other.name)
        {
            return true;
        }
        return false;
    }

    size_t TextureKeyHash::operator()(const TextureKey& key) const noexcept
    {
        size_t seed = 0;
        Hash::combine(seed, key.description.imageType);
        Hash::combine(seed, key.description.format);
        Hash::combine(seed, key.description.extent.width);
        Hash::combine(seed, key.description.extent.height);
        Hash::combine(seed, key.description.extent.depth);
        Hash::combine(seed, key.description.mipLevels);
        Hash::combine(seed, key.description.arrayLayers);
        Hash::combine(seed, key.description.samples);
        Hash::combine(seed, key.description.usage);
        Hash::combine(seed, key.name);
        return seed;
    }

    bool SubTextureKey::operator==(const SubTextureKey& other) const noexcept
    {
        if (description.aspectMask == other.description.aspectMask &&
            description.baseMipLevel == other.description.baseMipLevel &&
            description.levelCount == other.description.levelCount &&
            description.baseArrayLayer == other.description.baseArrayLayer &&
            description.layerCount == other.description.layerCount &&
            name == other.name)
        {
            return true;
        }
        return false;
    }

    size_t SubTextureKeyHash::operator()(const SubTextureKey& key) const noexcept
    {
        size_t seed = 0;
        Hash::combine(seed, key.description.aspectMask);
        Hash::combine(seed, key.description.baseMipLevel);
        Hash::combine(seed, key.description.levelCount);
        Hash::combine(seed, key.description.baseArrayLayer);
        Hash::combine(seed, key.description.layerCount);
        Hash::combine(seed, key.name);
        return seed;
    }

    VkFormat convertToVkFormat(AttachmentFormat format) noexcept
    {
        switch (format)
        {
        case AttachmentFormat::Undefined: return VK_FORMAT_UNDEFINED;
        case AttachmentFormat::R8_Unrom: return VK_FORMAT_R8_UNORM;
        case AttachmentFormat::R8_Uint: return VK_FORMAT_R8_UINT;
        case AttachmentFormat::R8G8_Unorm: return VK_FORMAT_R8G8_UNORM;
        case AttachmentFormat::R8G8_Uint: return VK_FORMAT_R8G8_UINT;
        case AttachmentFormat::R8G8B8_Unorm: return VK_FORMAT_R8G8B8_UNORM;
        case AttachmentFormat::R8G8B8_Srgb: return VK_FORMAT_R8G8B8_SRGB;
        case AttachmentFormat::R8G8B8_Uint: return VK_FORMAT_R8G8B8_UINT;
        case AttachmentFormat::R8G8B8A8_Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
        case AttachmentFormat::R8G8B8A8_Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
        case AttachmentFormat::R16_Unrom: return VK_FORMAT_R16_UNORM;
        case AttachmentFormat::R16_Uint: return VK_FORMAT_R16_UINT;
        case AttachmentFormat::R16_Sfloat: return VK_FORMAT_R16_SFLOAT;
        case AttachmentFormat::R16G16_Unorm: return VK_FORMAT_R16G16_UNORM;
        case AttachmentFormat::R16G16_Uint: return VK_FORMAT_R16G16_UINT;
        case AttachmentFormat::R16G16_Sfloat: return VK_FORMAT_R16G16_SFLOAT;
        case AttachmentFormat::R16G16B16_Unorm: return VK_FORMAT_R16G16B16_UNORM;
        case AttachmentFormat::R16G16B16_Uint: return VK_FORMAT_R16G16B16_UINT;
        case AttachmentFormat::R16G16B16_Sfloat: return VK_FORMAT_R16G16B16_SFLOAT;
        case AttachmentFormat::R16G16B16A16_Unorm: return VK_FORMAT_R16G16B16A16_UNORM;
        case AttachmentFormat::R16G16B16A16_Uint: return VK_FORMAT_R16G16B16A16_UINT;
        case AttachmentFormat::R16G16B16A16_Sfloat: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case AttachmentFormat::R32_Uint: return VK_FORMAT_R32_UINT;
        case AttachmentFormat::R32_Sfloat: return VK_FORMAT_R32_SFLOAT;
        case AttachmentFormat::R32G32_Uint: return VK_FORMAT_R32G32_UINT;
        case AttachmentFormat::R32G32_Sfloat: return VK_FORMAT_R32G32_SFLOAT;
        case AttachmentFormat::R32G32B32_Uint: return VK_FORMAT_R32G32B32_UINT;
        case AttachmentFormat::R32G32B32_Sfloat: return VK_FORMAT_R32G32B32_SFLOAT;
        case AttachmentFormat::R32G32B32A32_Uint: return VK_FORMAT_R32G32B32A32_UINT;
        case AttachmentFormat::R32G32B32A32_Sfloat: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case AttachmentFormat::A2R10G10B10_Unorm: return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
        case AttachmentFormat::A2B10G10R10_Unorm: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case AttachmentFormat::D16_Unorm: return VK_FORMAT_D16_UNORM;
        case AttachmentFormat::D24_Unorm_S8_Uint: return VK_FORMAT_D24_UNORM_S8_UINT;
        case AttachmentFormat::D32_Sfloat: return VK_FORMAT_D32_SFLOAT;
        case AttachmentFormat::D32_Sfloat_S8_Uint: return VK_FORMAT_D32_SFLOAT_S8_UINT;
        default: return VK_FORMAT_UNDEFINED;
        }
    }

    AttachmentFormat convertFromVkFormat(VkFormat format) noexcept
    {
        switch (format)
        {
        case VK_FORMAT_UNDEFINED: return AttachmentFormat::Undefined;
        case VK_FORMAT_R8_UNORM: return AttachmentFormat::R8_Unrom;
        case VK_FORMAT_R8_UINT: return AttachmentFormat::R8_Uint;
        case VK_FORMAT_R8G8_UNORM: return AttachmentFormat::R8G8_Unorm;
        case VK_FORMAT_R8G8_UINT: return AttachmentFormat::R8G8_Uint;
        case VK_FORMAT_R8G8B8_UNORM: return AttachmentFormat::R8G8B8_Unorm;
        case VK_FORMAT_R8G8B8_SRGB: return AttachmentFormat::R8G8B8_Srgb;
        case VK_FORMAT_R8G8B8_UINT: return AttachmentFormat::R8G8B8_Uint;
        case VK_FORMAT_R8G8B8A8_UNORM: return AttachmentFormat::R8G8B8A8_Unorm;
        case VK_FORMAT_R8G8B8A8_SRGB: return AttachmentFormat::R8G8B8A8_Srgb;
        case VK_FORMAT_R16_UNORM: return AttachmentFormat::R16_Unrom;
        case VK_FORMAT_R16_UINT: return AttachmentFormat::R16_Uint;
        case VK_FORMAT_R16_SFLOAT: return AttachmentFormat::R16_Sfloat;
        case VK_FORMAT_R16G16_UNORM: return AttachmentFormat::R16G16_Unorm;
        case VK_FORMAT_R16G16_UINT: return AttachmentFormat::R16G16_Uint;
        case VK_FORMAT_R16G16_SFLOAT: return AttachmentFormat::R16G16_Sfloat;
        case VK_FORMAT_R16G16B16_UNORM: return AttachmentFormat::R16G16B16_Unorm;
        case VK_FORMAT_R16G16B16_UINT: return AttachmentFormat::R16G16B16_Uint;
        case VK_FORMAT_R16G16B16_SFLOAT: return AttachmentFormat::R16G16B16_Sfloat;
        case VK_FORMAT_R16G16B16A16_UNORM: return AttachmentFormat::R16G16B16A16_Unorm;
        case VK_FORMAT_R16G16B16A16_UINT: return AttachmentFormat::R16G16B16A16_Uint;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return AttachmentFormat::R16G16B16A16_Sfloat;
        case VK_FORMAT_R32_UINT: return AttachmentFormat::R32_Uint;
        case VK_FORMAT_R32_SFLOAT: return AttachmentFormat::R32_Sfloat;
        case VK_FORMAT_R32G32_UINT: return AttachmentFormat::R32G32_Uint;
        case VK_FORMAT_R32G32_SFLOAT: return AttachmentFormat::R32G32_Sfloat;
        case VK_FORMAT_R32G32B32_UINT: return AttachmentFormat::R32G32B32_Uint;
        case VK_FORMAT_R32G32B32_SFLOAT: return AttachmentFormat::R32G32B32_Sfloat;
        case VK_FORMAT_R32G32B32A32_UINT: return AttachmentFormat::R32G32B32A32_Uint;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return AttachmentFormat::R32G32B32A32_Sfloat;
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32: return AttachmentFormat::A2R10G10B10_Unorm;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return AttachmentFormat::A2B10G10R10_Unorm;
        case VK_FORMAT_D16_UNORM: return AttachmentFormat::D16_Unorm;
        case VK_FORMAT_D24_UNORM_S8_UINT: return AttachmentFormat::D24_Unorm_S8_Uint;
        case VK_FORMAT_D32_SFLOAT: return AttachmentFormat::D32_Sfloat;
        case VK_FORMAT_D32_SFLOAT_S8_UINT: return AttachmentFormat::D32_Sfloat_S8_Uint;
        default: return AttachmentFormat::Undefined;
        }
    }

    VkAttachmentLoadOp convertToVkAttachmentLoadOp(AttachmentLoadOp loadOp) noexcept
    {
        switch (loadOp)
        {
        case AttachmentLoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
        case AttachmentLoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case AttachmentLoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        default: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        }
    }

    AttachmentLoadOp convertFromVkAttachmentLoadOp(VkAttachmentLoadOp loadOp) noexcept
    {
        switch (loadOp)
        {
        case VK_ATTACHMENT_LOAD_OP_LOAD: return AttachmentLoadOp::Load;
        case VK_ATTACHMENT_LOAD_OP_CLEAR: return AttachmentLoadOp::Clear;
        case VK_ATTACHMENT_LOAD_OP_DONT_CARE: return AttachmentLoadOp::DontCare;
        default: return AttachmentLoadOp::DontCare;
        }
    }

    VkAttachmentStoreOp convertToVkAttachmentStoreOp(AttachmentStoreOp storeOp) noexcept
    {
        switch (storeOp)
        {
        case AttachmentStoreOp::Store: return VK_ATTACHMENT_STORE_OP_STORE;
        case AttachmentStoreOp::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        default: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }
    }

    AttachmentFormat convertFromVkAttachmentStoreOp(VkAttachmentStoreOp storeOp) noexcept
    {
        switch (storeOp)
        {
        case VK_ATTACHMENT_STORE_OP_STORE: return AttachmentFormat::Undefined;
        case VK_ATTACHMENT_STORE_OP_DONT_CARE: return AttachmentFormat::Undefined;
        default: return AttachmentFormat::Undefined;
        }
    }

    VkClearColorValue convertToVkClearColorValue(ClearColorValue clearColor) noexcept
    {
        switch (clearColor)
        {
        case ClearColorValue::BlackOpaque:
            return { 0.0f, 0.0f, 0.0f, 1.0f };
        case ClearColorValue::WhiteOpaque:
            return { 1.0f, 1.0f, 1.0f, 1.0f };
        case ClearColorValue::BlackTransparent:
            return { 0.0f, 0.0f, 0.0f, 0.0f };
        case ClearColorValue::WhiteTransparent:
            return { 1.0f, 1.0f, 1.0f, 0.0f };
        default:
            return { 0.0f, 0.0f, 0.0f, 1.0f };
        }
    }

    VkClearDepthStencilValue convertToVkClearDepthStencilValue(ClearDepthStencilValue clearDepthStencilValue) noexcept
    {
        float depth = 1.0f;
        switch (clearDepthStencilValue.depth)
        {
        case ClearDepthValue::One: depth = 1.0f; break;
        case ClearDepthValue::Zero: depth = 0.0f; break;
        }
        return { depth, static_cast<uint32_t>(clearDepthStencilValue.stencil) };
    }

    VkImageLayout convertToVkImageLayout(ImageLayout layout) noexcept
    {
        switch (layout)
        {
        case ImageLayout::Undefined: return VK_IMAGE_LAYOUT_UNDEFINED;
        case ImageLayout::General: return VK_IMAGE_LAYOUT_GENERAL;
        case ImageLayout::Color_Attachment_Optimal: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case ImageLayout::Depth_Stencil_Attachment_Optimal: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case ImageLayout::Depth_Stencil_Read_Only_Optimal: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        case ImageLayout::Shader_Read_Only_Optimal: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case ImageLayout::Transfer_Src_Optimal: return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case ImageLayout::Transfer_Dst_Optimal: return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case ImageLayout::Depth_Read_Only_Stencil_Attachment_Optimal: return VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;
        case ImageLayout::Depth_Attachment_Stencil_Read_Only_Optimal: return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;
        case ImageLayout::Depth_Attachment_Optimal: return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        case ImageLayout::Depth_Read_Only_Optimal: return VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
        case ImageLayout::Stencil_Attachment_Optimal: return VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL;
        case ImageLayout::Stencil_Read_Only_Optimal: return VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL;
        case ImageLayout::Attachment_Optimal: return VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        case ImageLayout::Present_Src: return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        default: return VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }

    ImageLayout convertFromVkImageLayout(VkImageLayout layout) noexcept
    {
        switch (layout)
        {
        case VK_IMAGE_LAYOUT_UNDEFINED: return ImageLayout::Undefined;
        case VK_IMAGE_LAYOUT_GENERAL: return ImageLayout::General;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return ImageLayout::Color_Attachment_Optimal;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return ImageLayout::Depth_Stencil_Attachment_Optimal;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL: return ImageLayout::Depth_Stencil_Read_Only_Optimal;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return ImageLayout::Shader_Read_Only_Optimal;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return ImageLayout::Transfer_Src_Optimal;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return ImageLayout::Transfer_Dst_Optimal;
        case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL: return ImageLayout::Depth_Read_Only_Stencil_Attachment_Optimal;
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL: return ImageLayout::Depth_Attachment_Stencil_Read_Only_Optimal;
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL: return ImageLayout::Depth_Attachment_Optimal;
        case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL: return ImageLayout::Depth_Read_Only_Optimal;
        case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL: return ImageLayout::Stencil_Attachment_Optimal;
        case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL: return ImageLayout::Stencil_Read_Only_Optimal;
        case VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL: return ImageLayout::Attachment_Optimal;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: return ImageLayout::Present_Src;
        default: return ImageLayout::Undefined;
        }
    }

    bool AttachmentKey::operator==(const AttachmentKey& other) const noexcept
    {
        if (format == other.format &&
            loadOp == other.loadOp &&
            storeOp == other.storeOp &&
            layout == other.layout)
        {
            return true;
        }
        return false;
    }

    bool RenderPassKey::operator==(const RenderPassKey& other) const noexcept
    {
        for (uint32_t i = 0; i < kMaxColorAttachmentCount; ++i)
        {
            if (colorAttachments[i] != other.colorAttachments[i])
                return false;
        }
        if (depthStencilAttachment == other.depthStencilAttachment &&
            stencilLoadOp == other.stencilLoadOp &&
            stencilStoreOp == other.stencilStoreOp &&
            samples == other.samples &&
            flags == other.flags)
        {
            return true;
        }
        return false;
    }

    size_t RenderPassKeyHash::operator()(const RenderPassKey& key) const noexcept
    {
        size_t seed = 0;
        for (uint32_t i = 0; i < kMaxColorAttachmentCount; ++i)
        {
            Hash::combine(seed, key.colorAttachments[i].format);
            Hash::combine(seed, key.colorAttachments[i].loadOp);
            Hash::combine(seed, key.colorAttachments[i].storeOp);
            Hash::combine(seed, key.colorAttachments[i].layout);
        }
        Hash::combine(seed, key.depthStencilAttachment.format);
        Hash::combine(seed, key.depthStencilAttachment.loadOp);
        Hash::combine(seed, key.depthStencilAttachment.storeOp);
        Hash::combine(seed, key.depthStencilAttachment.layout);
        Hash::combine(seed, key.stencilLoadOp);
        Hash::combine(seed, key.stencilStoreOp);
        Hash::combine(seed, key.samples);
        return seed;
    }

    bool FramebufferKey::operator==(const FramebufferKey& other) const noexcept
    {
        for (uint32_t i = 0; i < kMaxColorAttachmentCount; ++i)
        {
            if (colors[i] != other.colors[i])
                return false;
        }
        if (renderPass == other.renderPass &&
            width == other.width &&
            height == other.height &&
            layers == other.layers &&
            samples == other.samples &&
            depthStencil == other.depthStencil)
        {
            return true;
        }
        return false;
    }

    size_t FramebufferKeyHash::operator()(const FramebufferKey& key) const noexcept
    {
        size_t seed = 0;
        Hash::combine(seed, key.renderPass);
        Hash::combine(seed, key.width);
        Hash::combine(seed, key.height);
        Hash::combine(seed, key.layers);
        Hash::combine(seed, key.samples);
        for (int i = 0; i < kMaxColorAttachmentCount; i++)
            Hash::combine(seed, key.colors[i]);
        Hash::combine(seed, key.depthStencil);
        return seed;
    }
}
