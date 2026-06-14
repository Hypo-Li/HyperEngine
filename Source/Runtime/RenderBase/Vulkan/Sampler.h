#pragma once
#include "Device.h"

namespace hyp::vk
{
    class Sampler : public Refable
    {
    public:
        struct CreateInfo
        {
            VkFilter magFilter = VK_FILTER_LINEAR;
            VkFilter minFilter = VK_FILTER_LINEAR;
            VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            float mipLodBias = 0.0f;
            VkBool32 anisotropyEnable = VK_FALSE;
            float maxAnisotropy = 0.0f;
            VkBool32 compareEnable = VK_FALSE;
            VkCompareOp compareOp = VK_COMPARE_OP_NEVER;
            float minLod = 0.0f;
            float maxLod = VK_LOD_CLAMP_NONE;
            VkBorderColor borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
            VkBool32 unnormalizedCoordinates = VK_FALSE;

            bool operator==(const CreateInfo& other) const noexcept;
        };

        struct CreateInfoHash
        {
            std::size_t operator()(const CreateInfo& info) const noexcept;
        };

        Sampler(const Device& device, const CreateInfo& createInfo) noexcept;
        ~Sampler() noexcept;

        VkSampler getHandle() const noexcept
        {
            return mSampler;
        }

    private:
        const Device& mDevice;
        VkSampler mSampler{ VK_NULL_HANDLE };
    };

    class SamplerCache
    {
    public:
        SamplerCache(const Device& device) noexcept;

        ~SamplerCache() noexcept = default;

        Sampler* get(const Sampler::CreateInfo& createInfo);

        Sampler* getLinearClampNoMip();

        Sampler* getPointClampNoMip();

        Sampler* getLinearRepeatNoMip();

        Sampler* getPointRepeatNoMip();

        Sampler* getLinearClampMip();

        Sampler* getPointClampMip();

        Sampler* getLinearRepeatMip();

        Sampler* getPointRepeatMip();

    private:
        const Device& mDevice;
        using SamplerMap = std::unordered_map<Sampler::CreateInfo, TRef<Sampler>, Sampler::CreateInfoHash>;
        SamplerMap mSamplerMap;

        TRef<Sampler> mLinearClampNoMip;
        TRef<Sampler> mPointClampNoMip;
        TRef<Sampler> mLinearRepeatNoMip;
        TRef<Sampler> mPointRepeatNoMip;
        TRef<Sampler> mLinearClampMip;
        TRef<Sampler> mPointClampMip;
        TRef<Sampler> mLinearRepeatMip;
        TRef<Sampler> mPointRepeatMip;
    };
}
