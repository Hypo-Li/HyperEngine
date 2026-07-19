#pragma once
#include <RHI/Sampler.h>
#include <RHI/Device.h>
#include <Core/Common/Refable.h>

#include <unordered_map>

namespace hyp
{
    class SamplerCache
    {
    public:
        SamplerCache(const rhi::Device& device);

        TRef<rhi::Sampler> getOrCreate(const rhi::SamplerDesc& desc);

        TRef<rhi::Sampler> getLinearClampNoMip();

        TRef<rhi::Sampler> getPointClampNoMip();

        TRef<rhi::Sampler> getLinearRepeatNoMip();

        TRef<rhi::Sampler> getPointRepeatNoMip();

        TRef<rhi::Sampler> getLinearClampMip();

        TRef<rhi::Sampler> getPointClampMip();

        TRef<rhi::Sampler> getLinearRepeatMip();

        TRef<rhi::Sampler> getPointRepeatMip();

    private:
        struct Key
        {
            rhi::SamplerDesc desc;

            bool operator==(const Key& other) const noexcept;
        };

        struct KeyHash
        {
            size_t operator()(const Key& key) const noexcept;
        };

        const rhi::Device& mDevice;
        std::unordered_map<Key, TRef<rhi::Sampler>, KeyHash> mCache;

        TRef<rhi::Sampler> mLinearClampNoMip;
        TRef<rhi::Sampler> mPointClampNoMip;
        TRef<rhi::Sampler> mLinearRepeatNoMip;
        TRef<rhi::Sampler> mPointRepeatNoMip;
        TRef<rhi::Sampler> mLinearClampMip;
        TRef<rhi::Sampler> mPointClampMip;
        TRef<rhi::Sampler> mLinearRepeatMip;
        TRef<rhi::Sampler> mPointRepeatMip;
    };
}
