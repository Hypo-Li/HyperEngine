#pragma once
#include "RenderGraphTypes.h"

namespace hyp::rg
{
    class TransientResources
    {
    public:
        TransientResources(const vk::Device& device);
        ~TransientResources();

        vk::Buffer* acquireBuffer(const BufferKey& key);

        vk::ImageView* acquireTexture(const TextureKey& key);

        vk::ImageView* acquireSubTexture(vk::ImageView* texture, const SubTextureKey& key);

        vk::RenderPass* acquireRenderPass(const RenderPassKey& key);

        vk::Framebuffer* acquireFramebuffer(const FramebufferKey& key);

        uint16_t getPassPrevBufferBarrierCount() const noexcept
        {
            return static_cast<uint16_t>(mPassPrevBufferBarriers.size());
        }

        void addPassPrevBufferBarrier(const VkBufferMemoryBarrier2& barrier)
        {
            mPassPrevBufferBarriers.emplace_back(barrier);
        }

        std::span<const VkBufferMemoryBarrier2> getPassPrevBufferBarriers(uint16_t begin, uint16_t count) const noexcept
        {
            return std::span<const VkBufferMemoryBarrier2>(mPassPrevBufferBarriers.data() + begin, count);
        }

        uint16_t getPassPrevImageBarrierCount() const noexcept
        {
            return static_cast<uint16_t>(mPassPrevImageBarriers.size());
        }

        void addPassPrevImageBarrier(const VkImageMemoryBarrier2& barrier)
        {
            mPassPrevImageBarriers.emplace_back(barrier);
        }

        std::span<const VkImageMemoryBarrier2> getPassPrevImageBarriers(uint16_t begin, uint16_t count) const noexcept
        {
            return std::span<const VkImageMemoryBarrier2>(mPassPrevImageBarriers.data() + begin, count);
        }

        uint16_t getPassPostBufferBarrierCount() const noexcept
        {
            return static_cast<uint16_t>(mPassPostBufferBarriers.size());
        }

        void addPassPostBufferBarrier(const VkBufferMemoryBarrier2& barrier)
        {
            mPassPostBufferBarriers.emplace_back(barrier);
        }

        std::span<const VkBufferMemoryBarrier2> getPassPostBufferBarriers(uint16_t begin, uint16_t count) const noexcept
        {
            return std::span<const VkBufferMemoryBarrier2>(mPassPostBufferBarriers.data() + begin, count);
        }

        uint16_t getPassPostImageBarrierCount() const noexcept
        {
            return static_cast<uint16_t>(mPassPostImageBarriers.size());
        }

        void addPassPostImageBarrier(const VkImageMemoryBarrier2& barrier)
        {
            mPassPostImageBarriers.emplace_back(barrier);
        }

        std::span<const VkImageMemoryBarrier2> getPassPostImageBarriers(uint16_t begin, uint16_t count) const noexcept
        {
            return std::span<const VkImageMemoryBarrier2>(mPassPostImageBarriers.data() + begin, count);
        }

        void destroyUselessResources();

        void clearBarriers();

        const vk::Device& getDevice() const noexcept
        {
            return mDevice;
        }

    private:
        static vk::Buffer* createBuffer(const vk::Device& device, const BufferKey& key);

        static std::pair<vk::Image*, vk::ImageView*> createTexture(const vk::Device& device, const TextureKey& key);

        static vk::ImageView* createSubTexture(vk::Image* image, const SubTextureKey& key);

        static vk::RenderPass* createRenderPass(const vk::Device& device, const RenderPassKey& key);

        static vk::Framebuffer* createFramebuffer(const vk::Device& device, const FramebufferKey& key);

    private:
        const vk::Device& mDevice;

        template <typename T, size_t MIN_REFCOUNT, size_t MAX_UNUSED_COUNT = 3>
        struct ResourceRecord
        {
            TRef<T> resource;
            uint32_t unusedCount = 0;

            ResourceRecord(T* res) : resource(res) {}

            bool shouldDestroy() noexcept
            {
                // 连续MAX_UNUSED_COUNT没有被使用则可销毁
                if (resource->getRefCount() == MIN_REFCOUNT)
                    ++unusedCount;
                else
                    unusedCount = 0;

                return unusedCount == MAX_UNUSED_COUNT;
            }
        };

        using BufferRecord = ResourceRecord<vk::Buffer, 1>;
        std::unordered_map<BufferKey, BufferRecord, BufferKeyHash> mBufferMap;

        using TextureRecord = ResourceRecord<vk::ImageView, 2>;
        std::unordered_map<TextureKey, TextureRecord, TextureKeyHash> mTextureMap;

        // key用于存放image, value为非默认imageView
        using SubTextureMap = std::unordered_map<SubTextureKey, TextureRecord, SubTextureKeyHash>;
        std::unordered_map<TRef<vk::Image>, SubTextureMap> mSubTextureMaps;

        using RenderPassRecord = ResourceRecord<vk::RenderPass, 1>;
        std::unordered_map<RenderPassKey, RenderPassRecord, RenderPassKeyHash> mRenderPassMap;

        using FramebufferRecord = ResourceRecord<vk::Framebuffer, 1>;
        std::unordered_map<FramebufferKey, FramebufferRecord, FramebufferKeyHash> mFramebufferMap;

        std::vector<VkBufferMemoryBarrier2> mPassPrevBufferBarriers;
        std::vector<VkBufferMemoryBarrier2> mPassPostBufferBarriers;
        std::vector<VkImageMemoryBarrier2> mPassPrevImageBarriers;
        std::vector<VkImageMemoryBarrier2> mPassPostImageBarriers;
    };
}
