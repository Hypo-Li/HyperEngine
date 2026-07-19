#pragma once
#include <vector>
#include <array>
#include <mutex>

namespace hyp
{
    enum class ProxyType : uint8_t
    {
        Texture,
        Sampler,
        Material,
        Geometry,

        Scene,
        Camera,
        Mesh_Render,
        Directional_Light,
        Atmosphere,
        Sky_Light,

        GSplat_Render,

        Count,
    };

    class ProxyHandle
    {
    public:
        ProxyHandle() = default;

        ProxyHandle(uint32_t index, uint32_t version) :
            mIndex(index), mVersion(version)
        {
        }

        bool operator==(const ProxyHandle& other) const noexcept
        {
            return mIndex == other.mIndex &&
                mVersion == other.mVersion;
        }

        uint32_t getIndex() const noexcept
        {
            return mIndex;
        }

        uint32_t getVersion() const noexcept
        {
            return mVersion;
        }

        bool isValid() const noexcept
        {
            return mIndex != UINT32_MAX &&
                mVersion != 0;
        }

        void reset() noexcept
        {
            mIndex = 0;
            mVersion = 0;
        }

    private:
        uint32_t mIndex = UINT32_MAX;
        uint32_t mVersion = 0;
    };

    template <ProxyType PROXY_TYPE>
    class TProxyHandle : public ProxyHandle
    {
    public:
        TProxyHandle() = default;

        TProxyHandle(uint32_t index, uint32_t version)
            : ProxyHandle(index, version)
        {
        }
    };

    using TextureProxyHandle = TProxyHandle<ProxyType::Texture>;

    using SamplerProxyHandle = TProxyHandle<ProxyType::Sampler>;

    using MaterialProxyHandle = TProxyHandle<ProxyType::Material>;

    using GeometryProxyHandle = TProxyHandle<ProxyType::Geometry>;

    using SceneProxyHandle = TProxyHandle<ProxyType::Scene>;

    using CameraProxyHandle = TProxyHandle<ProxyType::Camera>;

    using MeshRenderProxyHandle = TProxyHandle<ProxyType::Mesh_Render>;

    using DirectionalLightProxyHandle = TProxyHandle<ProxyType::Directional_Light>;

    using AtmosphereProxyHandle = TProxyHandle<ProxyType::Atmosphere>;

    using SkyLightProxyHandle = TProxyHandle<ProxyType::Sky_Light>;

    using GSplatRenderProxyHandle = TProxyHandle<ProxyType::GSplat_Render>;

    class ProxyHandleAllocator
    {
    public:
        ProxyHandleAllocator() = default;
        ~ProxyHandleAllocator() = default;

        template <ProxyType PROXY_TYPE>
        TProxyHandle<PROXY_TYPE> allocate()
        {
            auto& pool = mPools[static_cast<uint8_t>(PROXY_TYPE)];
            uint32_t index;
            uint32_t version;

            if (!pool.freeIndices.empty())
            {
                // 1. 从空闲池回收索引
                index = pool.freeIndices.back();
                pool.freeIndices.pop_back();
                version = pool.versions[index];
            }
            else
            {
                // 2. 分配全新索引
                index = static_cast<uint32_t>(pool.versions.size());
                version = 1;
                pool.versions.push_back(version);
            }

            return TProxyHandle<PROXY_TYPE>(index, version);
        }

        template <ProxyType PROXY_TYPE>
        void free(TProxyHandle<PROXY_TYPE> handle)
        {
            assert(isHandleValid(handle) && "Invalid handle");

            auto& pool = mPools[static_cast<uint8_t>(PROXY_TYPE)];
            uint32_t index = handle.getIndex();

            if (pool.versions[index] == handle.getVersion())
            {
                // 1. 递增版本号, 使所有持有该版本句柄的对象立即失效
                pool.versions[index]++;
                pool.freeIndices.push_back(index);
            }
        }

        template <ProxyType PROXY_TYPE>
        bool isHandleValid(TProxyHandle<PROXY_TYPE> handle) const noexcept
        {
            auto& pool = mPools[static_cast<uint8_t>(PROXY_TYPE)];
            return handle.getIndex() < pool.versions.size() && handle.getVersion() == pool.versions[handle.getIndex()];
        }

        template <ProxyType PROXY_TYPE>
        size_t getAllocatedCount() const
        {
            const auto& pool = mPools[static_cast<uint8_t>(PROXY_TYPE)];
            return pool.versions.size() - pool.freeIndices.size();
        }

    private:
        struct TypedPool
        {
            std::vector<uint32_t> versions;
            std::vector<uint32_t> freeIndices;

            TypedPool()
            {
                versions.reserve(1024);
                freeIndices.reserve(256);
            }
        };

        std::array<TypedPool, static_cast<size_t>(ProxyType::Count)> mPools;
    };
}
