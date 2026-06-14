#pragma once
#include "RenderPass.h"

namespace hyp::vk
{
    class DummyRenderPassCache
    {
    public:
        DummyRenderPassCache(const Device& device);
        virtual ~DummyRenderPassCache() = default;

        RenderPass* getOrCreateDummyRenderPass(std::span<const VkFormat> colorFormats, VkFormat depthStencilFormat, VkSampleCountFlagBits samples);

    private:
        const Device& mDevice;

        struct Key
        {
            std::vector<VkFormat> colorFormats;
            VkFormat depthStencilFormat = VK_FORMAT_UNDEFINED;
            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;

            bool operator==(const Key& other) const;
        };

        struct KeyHash
        {
            size_t operator()(const Key& key) const;
        };

        std::unordered_map<Key, TRef<RenderPass>, KeyHash> mDummyRenderPassMap;
    };
}
