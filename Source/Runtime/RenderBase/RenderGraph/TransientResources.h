#pragma once
#include "RenderGraphTypes.h"

#include <type_traits>

namespace hyp::rg
{
    class TransientResources
    {
    public:
        TransientResources(const rhi::Device& device);

        ~TransientResources();

        static constexpr uint32_t kDefaultMaxUnusedFrames = 3;

        TRef<rhi::Buffer> acquireBuffer(const BufferKey& key, uint32_t maxUnusedFrames = kDefaultMaxUnusedFrames);

        TRef<rhi::TextureView> acquireTexture(const TextureKey& key, uint32_t maxUnusedFrames = kDefaultMaxUnusedFrames);

        TRef<rhi::TextureView> acquireSubTexture(const rhi::TextureView& texture, const SubTextureKey& key);

        uint16_t getPrePassBufferBarrierCount() const noexcept
        {
            return static_cast<uint16_t>(mPrePassBufferBarriers.size());
        }

        void addPrePassBufferBarrier(rhi::BufferBarrier transition)
        {
            mPrePassBufferBarriers.emplace_back(transition);
        }

        std::span<const rhi::BufferBarrier> getPrePassBufferBarriers(uint16_t begin, uint16_t count) const noexcept
        {
            return std::span<const rhi::BufferBarrier>(mPrePassBufferBarriers.data() + begin, count);
        }

        uint16_t getPrePassTextureBarrierCount() const noexcept
        {
            return static_cast<uint16_t>(mPrePassTextureBarriers.size());
        }

        void addPrePassTextureBarrier(rhi::TextureBarrier transition)
        {
            mPrePassTextureBarriers.emplace_back(transition);
        }

        std::span<const rhi::TextureBarrier> getPrePassTextureBarriers(uint16_t begin, uint16_t count) const noexcept
        {
            return std::span<const rhi::TextureBarrier>(mPrePassTextureBarriers.data() + begin, count);
        }

        uint16_t getPostPassBufferBarrierCount() const noexcept
        {
            return static_cast<uint16_t>(mPostPassBufferBarriers.size());
        }

        void addPostPassBufferBarrier(rhi::BufferBarrier transition)
        {
            mPostPassBufferBarriers.emplace_back(transition);
        }

        std::span<const rhi::BufferBarrier> getPostPassBufferBarriers(uint16_t begin, uint16_t count) const noexcept
        {
            return std::span<const rhi::BufferBarrier>(mPostPassBufferBarriers.data() + begin, count);
        }

        uint16_t getPostPassTextureBarrierCount() const noexcept
        {
            return static_cast<uint16_t>(mPostPassTextureBarriers.size());
        }

        void addPostPassTextureBarrier(rhi::TextureBarrier transition)
        {
            mPostPassTextureBarriers.emplace_back(transition);
        }

        std::span<const rhi::TextureBarrier> getPostPassTextureBarriers(uint16_t begin, uint16_t count) const noexcept
        {
            return std::span<const rhi::TextureBarrier>(mPostPassTextureBarriers.data() + begin, count);
        }

        void destroyUselessResources();

        void clearBarriers();

        const rhi::Device& getDevice() const noexcept
        {
            return mDevice;
        }

    private:
        static TRef<rhi::Buffer> createBuffer(const rhi::Device& device, const BufferKey& key);

        static std::pair<TRef<rhi::Texture>, TRef<rhi::TextureView>> createTexture(const rhi::Device& device, const TextureKey& key);

        static TRef<rhi::TextureView> createSubTexture(rhi::Texture& texture, const SubTextureKey& key);

    private:
        const rhi::Device& mDevice;

        template <typename T>
        struct ResourceRecord
        {
            TRef<T> resource;
            uint32_t unusedCount = 0;
            uint32_t maxUnusedFrames;

            ResourceRecord(T* res, uint32_t maxUnusedFrames) : resource(res), maxUnusedFrames(maxUnusedFrames) {}

            bool shouldDestroy() noexcept
            {
                // 连续maxUnusedFrames帧没有被使用则可销毁
                if (isUnused())
                    ++unusedCount;
                else
                    unusedCount = 0;

                return unusedCount == maxUnusedFrames;
            }

        private:
            bool isUnused() const noexcept
            {
                if constexpr (std::is_same_v<T, rhi::Texture>)
                    return resource->getRefCount() == 1 && resource->getViewCount() == 0;
                else
                    return resource->getRefCount() == 1;
            }
        };

        using BufferRecord = ResourceRecord<rhi::Buffer>;
        std::unordered_map<BufferKey, BufferRecord, BufferKeyHash> mBufferMap;

        using TextureRecord = ResourceRecord<rhi::TextureView>;
        std::unordered_map<TextureKey, TextureRecord, TextureKeyHash> mTextureMap;

        // key用于存放texture, value为非默认textureView
        using SubTextureMap = std::unordered_map<SubTextureKey, TextureRecord, SubTextureKeyHash>;
        std::unordered_map<rhi::Texture*, SubTextureMap> mSubTextureMaps;

        using TextureOwnerRecord = ResourceRecord<rhi::Texture>;

        // TextureView 对 Texture 只持裸引用, 存活完全靠这里的 TRef
        std::unordered_map<rhi::Texture*, TextureOwnerRecord> mOwnedTextures;

        std::vector<rhi::BufferBarrier> mPrePassBufferBarriers;
        std::vector<rhi::TextureBarrier> mPrePassTextureBarriers;
        std::vector<rhi::BufferBarrier> mPostPassBufferBarriers;
        std::vector<rhi::TextureBarrier> mPostPassTextureBarriers;
    };
}
