#include "Sampler.h"

namespace hyp::vk
{
    static VkFilter convertFilter(rhi::Filter filter) noexcept
    {
        switch (filter)
        {
        case rhi::Filter::Nearest: return VK_FILTER_NEAREST;
        case rhi::Filter::Linear:  return VK_FILTER_LINEAR;
        default:                   return VK_FILTER_LINEAR;
        }
    }

    static VkSamplerMipmapMode convertMipmapMode(rhi::MipmapMode mode) noexcept
    {
        switch (mode)
        {
        case rhi::MipmapMode::Nearest: return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        case rhi::MipmapMode::Linear:  return VK_SAMPLER_MIPMAP_MODE_LINEAR;
        default:                       return VK_SAMPLER_MIPMAP_MODE_LINEAR;
        }
    }

    static VkSamplerAddressMode convertAddressMode(rhi::AddressMode mode) noexcept
    {
        switch (mode)
        {
        case rhi::AddressMode::Repeat:          return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case rhi::AddressMode::Mirrored_Repeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case rhi::AddressMode::Clamp_To_Edge:   return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        default:                                return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    }

    static VkCompareOp convertCompareOp(rhi::CompareOp op) noexcept
    {
        switch (op)
        {
        case rhi::CompareOp::Never:         return VK_COMPARE_OP_NEVER;
        case rhi::CompareOp::Less:          return VK_COMPARE_OP_LESS;
        case rhi::CompareOp::Equal:         return VK_COMPARE_OP_EQUAL;
        case rhi::CompareOp::Less_Equal:    return VK_COMPARE_OP_LESS_OR_EQUAL;
        case rhi::CompareOp::Greater:       return VK_COMPARE_OP_GREATER;
        case rhi::CompareOp::Not_Equal:     return VK_COMPARE_OP_NOT_EQUAL;
        case rhi::CompareOp::Greater_Equal: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case rhi::CompareOp::Always:        return VK_COMPARE_OP_ALWAYS;
        default:                            return VK_COMPARE_OP_NEVER;
        }
    }

    Sampler::Sampler(const Device& device, const rhi::SamplerDesc& desc)
        : rhi::Sampler(device)
    {
        VkSamplerCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        createInfo.magFilter = convertFilter(desc.magFilter);
        createInfo.minFilter = convertFilter(desc.minFilter);
        createInfo.mipmapMode = convertMipmapMode(desc.mipmapMode);
        createInfo.addressModeU = convertAddressMode(desc.addressModeU);
        createInfo.addressModeV = convertAddressMode(desc.addressModeV);
        createInfo.addressModeW = convertAddressMode(desc.addressModeW);
        createInfo.mipLodBias = desc.mipLodBias;
        createInfo.anisotropyEnable = desc.anisotropyEnable ? VK_TRUE : VK_FALSE;
        createInfo.maxAnisotropy = desc.maxAnisotropy;
        createInfo.compareEnable = desc.compareEnable ? VK_TRUE : VK_FALSE;
        createInfo.compareOp = convertCompareOp(desc.compareOp);
        createInfo.minLod = desc.minLod;
        createInfo.maxLod = desc.maxLod;
        createInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        createInfo.unnormalizedCoordinates = VK_FALSE;

        vkCreateSampler(device.getHandle(), &createInfo, nullptr, &mSampler);
    }

    Sampler::~Sampler() noexcept
    {
        vkDestroySampler(getVkDevice().getHandle(), mSampler, nullptr);
    }
}
