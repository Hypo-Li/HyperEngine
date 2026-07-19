#include "Sampler.h"

namespace hyp::wgpu
{
    static WGPUFilterMode convertFilter(rhi::Filter filter) noexcept
    {
        switch (filter)
        {
        case rhi::Filter::Nearest: return WGPUFilterMode_Nearest;
        case rhi::Filter::Linear:  return WGPUFilterMode_Linear;
        default:                   return WGPUFilterMode_Linear;
        }
    }

    static WGPUMipmapFilterMode convertMipmapMode(rhi::MipmapMode mode) noexcept
    {
        switch (mode)
        {
        case rhi::MipmapMode::Nearest: return WGPUMipmapFilterMode_Nearest;
        case rhi::MipmapMode::Linear:  return WGPUMipmapFilterMode_Linear;
        default:                       return WGPUMipmapFilterMode_Linear;
        }
    }

    static WGPUAddressMode convertAddressMode(rhi::AddressMode mode) noexcept
    {
        switch (mode)
        {
        case rhi::AddressMode::Repeat:          return WGPUAddressMode_Repeat;
        case rhi::AddressMode::Mirrored_Repeat: return WGPUAddressMode_MirrorRepeat;
        case rhi::AddressMode::Clamp_To_Edge:   return WGPUAddressMode_ClampToEdge;
        default:                                return WGPUAddressMode_Repeat;
        }
    }

    static WGPUCompareFunction convertCompareOp(rhi::CompareOp op) noexcept
    {
        switch (op)
        {
        case rhi::CompareOp::Never:         return WGPUCompareFunction_Never;
        case rhi::CompareOp::Less:          return WGPUCompareFunction_Less;
        case rhi::CompareOp::Equal:         return WGPUCompareFunction_Equal;
        case rhi::CompareOp::Less_Equal:    return WGPUCompareFunction_LessEqual;
        case rhi::CompareOp::Greater:       return WGPUCompareFunction_Greater;
        case rhi::CompareOp::Not_Equal:     return WGPUCompareFunction_NotEqual;
        case rhi::CompareOp::Greater_Equal: return WGPUCompareFunction_GreaterEqual;
        case rhi::CompareOp::Always:        return WGPUCompareFunction_Always;
        default:                            return WGPUCompareFunction_Never;
        }
    }

    Sampler::Sampler(const Device& device, const rhi::SamplerDesc& desc)
        : rhi::Sampler(device)
    {
        WGPUSamplerDescriptor samplerDesc = {};
        samplerDesc.nextInChain = nullptr;
        samplerDesc.label = WGPUStringView{ "UnnamedSampler", WGPU_STRLEN };
        samplerDesc.addressModeU = convertAddressMode(desc.addressModeU);
        samplerDesc.addressModeV = convertAddressMode(desc.addressModeV);
        samplerDesc.addressModeW = convertAddressMode(desc.addressModeW);
        samplerDesc.magFilter = convertFilter(desc.magFilter);
        samplerDesc.minFilter = convertFilter(desc.minFilter);
        samplerDesc.mipmapFilter = convertMipmapMode(desc.mipmapMode);
        samplerDesc.lodMinClamp = desc.minLod;
        samplerDesc.lodMaxClamp = desc.maxLod;
        samplerDesc.compare = desc.compareEnable ? convertCompareOp(desc.compareOp) : WGPUCompareFunction_Undefined;
        samplerDesc.maxAnisotropy = desc.anisotropyEnable ? static_cast<uint16_t>(desc.maxAnisotropy) : 1;

        mSampler = wgpuDeviceCreateSampler(device.getHandle(), &samplerDesc);
    }

    Sampler::~Sampler() noexcept
    {
        if (mSampler)
        {
            wgpuSamplerRelease(mSampler);
            mSampler = nullptr;
        }
    }
}
