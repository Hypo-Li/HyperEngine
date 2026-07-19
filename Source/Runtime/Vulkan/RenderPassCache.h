#pragma once
#include "Device.h"

#include <Core/Common/Hash.h>

#include <volk.h>
#include <array>
#include <span>
#include <unordered_map>

namespace hyp::vk
{
    struct RenderPassKey
    {
        uint32_t colorAttachmentCount = 0;
        std::array<VkFormat, 8> colorFormats{};
        std::array<VkAttachmentLoadOp, 8> colorLoadOps{};
        std::array<VkAttachmentStoreOp, 8> colorStoreOps{};

        VkFormat depthStencilFormat = VK_FORMAT_UNDEFINED;
        VkAttachmentLoadOp depthLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        VkAttachmentStoreOp depthStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        VkAttachmentLoadOp stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        VkAttachmentStoreOp stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        VkImageLayout depthStencilImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;

        bool operator==(const RenderPassKey& other) const noexcept
        {
            return colorAttachmentCount == other.colorAttachmentCount
                && colorFormats == other.colorFormats
                && colorLoadOps == other.colorLoadOps
                && colorStoreOps == other.colorStoreOps
                && depthStencilFormat == other.depthStencilFormat
                && depthLoadOp == other.depthLoadOp
                && depthStoreOp == other.depthStoreOp
                && stencilLoadOp == other.stencilLoadOp
                && stencilStoreOp == other.stencilStoreOp
                && depthStencilImageLayout == other.depthStencilImageLayout
                && sampleCount == other.sampleCount;
        }
    };

    struct RenderPassKeyHash
    {
        std::size_t operator()(const RenderPassKey& key) const noexcept
        {
            std::size_t hash = 0;
            for (uint32_t i = 0; i < key.colorAttachmentCount; ++i)
            {
                Hash::combine(hash, key.colorFormats[i]);
                Hash::combine(hash, key.colorLoadOps[i]);
                Hash::combine(hash, key.colorStoreOps[i]);
            }
            Hash::combine(hash, key.depthStencilFormat);
            Hash::combine(hash, key.depthLoadOp);
            Hash::combine(hash, key.depthStoreOp);
            Hash::combine(hash, key.stencilLoadOp);
            Hash::combine(hash, key.stencilStoreOp);
            Hash::combine(hash, key.depthStencilImageLayout);
            Hash::combine(hash, key.sampleCount);
            return hash;
        }
    };

    class RenderPassCache
    {
    public:
        explicit RenderPassCache(const Device& device);
        ~RenderPassCache();

        VkRenderPass getOrCreateRenderPass(const RenderPassKey& key);

        // Helper for pipeline creation: ops are irrelevant for pipeline compatibility.
        VkRenderPass getOrCreateRenderPass(
            std::span<const VkFormat> colorFormats,
            VkFormat depthStencilFormat,
            VkFormat depthFormat,
            VkFormat stencilFormat,
            VkSampleCountFlagBits sampleCount
        );

        // Helper for command list: uses actual load/store ops.
        VkRenderPass getOrCreateRenderPass(
            std::span<const VkFormat> colorFormats,
            std::span<const VkAttachmentLoadOp> colorLoadOps,
            std::span<const VkAttachmentStoreOp> colorStoreOps,
            VkFormat depthStencilFormat,
            VkAttachmentLoadOp depthLoadOp,
            VkAttachmentStoreOp depthStoreOp,
            VkAttachmentLoadOp stencilLoadOp,
            VkAttachmentStoreOp stencilStoreOp,
            VkImageLayout depthStencilImageLayout,
            VkSampleCountFlagBits sampleCount
        );

    private:
        const Device& mDevice;
        std::unordered_map<RenderPassKey, VkRenderPass, RenderPassKeyHash> mCache;
    };

    struct FramebufferKey
    {
        VkRenderPass renderPass = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t layers = 1;
        uint32_t attachmentCount = 0;
        std::array<VkImageView, 9> attachments{}; // 8 color + 1 depth-stencil

        bool operator==(const FramebufferKey& other) const noexcept
        {
            return renderPass == other.renderPass
                && width == other.width
                && height == other.height
                && layers == other.layers
                && attachmentCount == other.attachmentCount
                && attachments == other.attachments;
        }
    };

    struct FramebufferKeyHash
    {
        std::size_t operator()(const FramebufferKey& key) const noexcept
        {
            std::size_t hash = 0;
            hyp::Hash::combine(hash, key.renderPass);
            hyp::Hash::combine(hash, key.width);
            hyp::Hash::combine(hash, key.height);
            hyp::Hash::combine(hash, key.layers);
            hyp::Hash::combine(hash, key.attachmentCount);
            for (uint32_t i = 0; i < 9; ++i)
            {
                hyp::Hash::combine(hash, key.attachments[i]);
            }
            return hash;
        }
    };

    class FramebufferCache
    {
    public:
        explicit FramebufferCache(const Device& device);
        ~FramebufferCache();

        VkFramebuffer getOrCreateFramebuffer(
            VkRenderPass renderPass,
            std::span<const VkImageView> attachments,
            uint32_t width,
            uint32_t height,
            uint32_t layers
        );

    private:
        const Device& mDevice;
        std::unordered_map<FramebufferKey, VkFramebuffer, FramebufferKeyHash> mCache;
    };
}
