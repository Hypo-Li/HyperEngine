#include "SampleState.h"

namespace hyp
{
    template <> Type* Reflection::createType<Filter>()
    {
        auto e = new TEnumeration<Filter>("Filter");
        e->addValue("Point", Filter::Point);
        e->addValue("Bilinear", Filter::Bilinear);
        e->addValue("Trilinear", Filter::Trilinear);
        e->addValue("Anisotropic", Filter::Anisotropic);
        e->addValue("Global", Filter::Global);
        return e;
    }

    template <> Type* Reflection::createType<AddressMode>()
    {
        auto e = new TEnumeration<AddressMode>("AddressMode");
        e->addValue("Repeat", AddressMode::Repeat);
        e->addValue("Clamp", AddressMode::Clamp);
        e->addValue("Mirror", AddressMode::Mirror);
        return e;
    }

    template <> Type* Reflection::createType<SampleState>()
    {
        auto s = new TStructure<SampleState>("SampleState");
        s->addProperty("Filter", &SampleState::filter);
        s->addProperty("Address Mode U", &SampleState::addressModeU);
        s->addProperty("Address Mode V", &SampleState::addressModeV);
        s->addProperty("Address Mode W", &SampleState::addressModeW);
        return s;
    }

    static rhi::AddressMode toRHIAddressMode(AddressMode addressMode) noexcept
    {
        switch (addressMode)
        {
        case AddressMode::Repeat: return rhi::AddressMode::Repeat;
        case AddressMode::Clamp: return rhi::AddressMode::Clamp_To_Edge;
        case AddressMode::Mirror: return rhi::AddressMode::Mirrored_Repeat;
        default: return rhi::AddressMode::Repeat;
        }
    }

    rhi::SamplerDesc makeSamplerDesc(const SampleState& sampleState, const GlobalSamplerSettings& globalSettings) noexcept
    {
        // globalSettings.defaultFilter 本身不应该是 Global(config.json 手填错了才可能出现),
        // 这里不递归解析、直接兜底成 Trilinear, 避免自我引用死循环
        Filter filter = sampleState.filter == Filter::Global
            ? (globalSettings.defaultFilter == Filter::Global ? Filter::Trilinear : globalSettings.defaultFilter)
            : sampleState.filter;

        rhi::SamplerDesc samplerDesc;
        switch (filter)
        {
        case Filter::Point:
        {
            samplerDesc.minFilter = rhi::Filter::Nearest;
            samplerDesc.magFilter = rhi::Filter::Nearest;
            samplerDesc.mipmapMode = rhi::MipmapMode::Nearest;
            break;
        }
        case Filter::Bilinear:
        {
            samplerDesc.minFilter = rhi::Filter::Linear;
            samplerDesc.magFilter = rhi::Filter::Linear;
            samplerDesc.mipmapMode = rhi::MipmapMode::Nearest;
            break;
        }
        case Filter::Trilinear:
        {
            samplerDesc.minFilter = rhi::Filter::Linear;
            samplerDesc.magFilter = rhi::Filter::Linear;
            samplerDesc.mipmapMode = rhi::MipmapMode::Linear;
            break;
        }
        case Filter::Anisotropic:
        {
            samplerDesc.minFilter = rhi::Filter::Linear;
            samplerDesc.magFilter = rhi::Filter::Linear;
            samplerDesc.mipmapMode = rhi::MipmapMode::Linear;
            samplerDesc.anisotropyEnable = true;
            samplerDesc.maxAnisotropy = globalSettings.maxAnisotropy;
            break;
        }
        default:
        {
            assert(false && "Invalid Filter");
            break;
        }
        }

        samplerDesc.addressModeU = toRHIAddressMode(sampleState.addressModeU);
        samplerDesc.addressModeV = toRHIAddressMode(sampleState.addressModeV);
        samplerDesc.addressModeW = toRHIAddressMode(sampleState.addressModeW);
        samplerDesc.mipLodBias = globalSettings.mipLodBias;
        return samplerDesc;
    }
}
