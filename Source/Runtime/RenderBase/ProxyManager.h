#pragma once
#include "RenderProxy.h"

namespace hyp
{
    class ProxyPool
    {
    public:
        ProxyPool(RenderBackend& renderBackend) : mRenderBackend(renderBackend) {}

        ~ProxyPool() noexcept = default;

    protected:
        void pushProxyConstructionParameters(uint32_t index)
        {
            RenderProxy::sContextStack.emplace_back(&mRenderBackend, index);
        }

        void popProxyConstructionParameters()
        {
            RenderProxy::sContextStack.pop_back();
        }

    private:
        RenderBackend& mRenderBackend;
    };

    template <ProxyType PROXY_TYPE, AllocateFrequency ALLOC_FREQ = AllocateFrequency::Medium>
    class TProxyPool : public ProxyPool
    {
        using Proxy = typename TProxyTraits<PROXY_TYPE>::Proxy;
        using Handle = TProxyHandle<PROXY_TYPE>;
    public:
        TProxyPool(RenderBackend& renderBackend) : ProxyPool(renderBackend)
        {
            mVersions.resize(kChunkCapacity, 0);
            mChunks.resize(1);
        }

        ~TProxyPool() noexcept
        {
            for (uint32_t i = 0; i < mVersions.size(); ++i)
            {
                if (mVersions[i] != 0)
                {
                    uint32_t ci = i / kChunkCapacity, si = i % kChunkCapacity;
                    std::destroy_at(std::launder(reinterpret_cast<Proxy*>(mChunks[ci]->slots[si].storage)));
                }
            }
        }

        template <typename... Args>
        Proxy* create(Handle handle, Args&&... args)
        {
            uint32_t index = handle.getIndex();
            while (index >= mVersions.size())
            {
                mVersions.resize(index + 1, 0);
            }
            assert(mVersions[index] == 0);
            mVersions[index] = handle.getVersion();

            uint32_t chunkIndex = index / kChunkCapacity;
            while (chunkIndex >= mChunks.size())
            {
                mChunks.resize(chunkIndex + 1);
            }
            auto& chunk = mChunks[chunkIndex];
            if (!chunk)
            {
                chunk = std::make_unique<Chunk>();
            }

            uint32_t slotIndex = index % kChunkCapacity;
            auto& slot = chunk->slots[slotIndex];

            pushProxyConstructionParameters(index);
            Proxy* proxy = std::construct_at(
               reinterpret_cast<Proxy*>(slot.storage),
               std::forward<Args>(args)...
            );
            popProxyConstructionParameters();
            return proxy;
        }

        Proxy* get(Handle handle)
        {
            uint32_t index = handle.getIndex();
            if (index >= mVersions.size())
                return nullptr;
            if (mVersions[index] != handle.getVersion())
                return nullptr;
            uint32_t chunkIndex = index / kChunkCapacity;
            assert(chunkIndex < mChunks.size() && mChunks[chunkIndex]);
            uint32_t slotIndex = index % kChunkCapacity;
            return std::launder(reinterpret_cast<Proxy*>(mChunks[chunkIndex]->slots[slotIndex].storage));
        }

        void destroy(Handle handle)
        {
            uint32_t index = handle.getIndex();
            if (index >= mVersions.size())
                return;
            assert(mVersions[index] == handle.getVersion());
            uint32_t chunkIndex = index / kChunkCapacity;
            assert(chunkIndex < mChunks.size() && mChunks[chunkIndex]);
            uint32_t slotIndex = index % kChunkCapacity;
            Proxy* proxy = std::launder(reinterpret_cast<Proxy*>(mChunks[chunkIndex]->slots[slotIndex].storage));
            std::destroy_at(proxy);
            mVersions[index] = 0;
        }

    private:
        std::vector<uint32_t> mVersions;

        static constexpr size_t kChunkCapacity = poolChunkCapacity<Proxy, ALLOC_FREQ>();
        struct Chunk
        {
            struct Slot { alignas(Proxy) uint8_t storage[sizeof(Proxy)]; };
            std::array<Slot, kChunkCapacity> slots;
        };
        std::vector<std::unique_ptr<Chunk>> mChunks;
    };

    class RenderBackend;
    class ProxyManager
    {
    public:
        ProxyManager(RenderBackend& renderBackend) : mRenderBackend(renderBackend) {}

        ~ProxyManager() noexcept = default;

        template <ProxyType PROXY_TYPE, typename... Args>
        auto createProxy(TProxyHandle<PROXY_TYPE> handle, Args&&... args)
        {
            static constexpr size_t poolIndex = static_cast<size_t>(PROXY_TYPE);
            if (!mProxyPools[poolIndex])
            {
                mProxyPools[poolIndex] = std::make_unique<TProxyPool<PROXY_TYPE>>(mRenderBackend);
            }
            TProxyPool<PROXY_TYPE>* proxyPool = static_cast<TProxyPool<PROXY_TYPE>*>(mProxyPools[poolIndex].get());
            return proxyPool->create(handle, std::forward<Args>(args)...);
        }

        template <ProxyType PROXY_TYPE>
        auto getProxy(TProxyHandle<PROXY_TYPE> handle) const
        {
            static constexpr size_t poolIndex = static_cast<size_t>(PROXY_TYPE);
            assert(mProxyPools[poolIndex]);
            TProxyPool<PROXY_TYPE>* proxyPool = static_cast<TProxyPool<PROXY_TYPE>*>(mProxyPools[poolIndex].get());
            return proxyPool->get(handle);
        }

        template <ProxyType PROXY_TYPE>
        void destroyProxy(TProxyHandle<PROXY_TYPE> handle)
        {
            static constexpr size_t poolIndex = static_cast<size_t>(PROXY_TYPE);
            assert(mProxyPools[poolIndex]);
            TProxyPool<PROXY_TYPE>* proxyPool = static_cast<TProxyPool<PROXY_TYPE>*>(mProxyPools[poolIndex].get());
            proxyPool->destroy(handle);
        }

    private:
        RenderBackend& mRenderBackend;
        static constexpr size_t kProxyTypeCount = static_cast<size_t>(ProxyType::Count);
        std::array<std::unique_ptr<ProxyPool>, kProxyTypeCount> mProxyPools;
    };
}
