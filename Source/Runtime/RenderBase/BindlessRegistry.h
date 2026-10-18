#pragma once
#include "RenderBaseDefines.h"

#include <RHI/Texture.h>
#include <RHI/Sampler.h>

namespace hyp
{
    enum class BindlessBinding : uint32_t
    {
        Unknown = UINT32_MAX,
        Texture2D = 0,
        Texture2DArray,
        Texture3D,
        TextureCube,
        Sampler,
    };

    struct BindlessHandle
    {
        BindlessBinding binding = BindlessBinding::Unknown;
        uint32_t index = UINT32_MAX;

        BindlessHandle() = default;

        BindlessHandle(BindlessBinding binding, uint32_t index) :
            binding(binding), index(index)
        {
        }

        bool isValid() const noexcept
        {
            return binding != BindlessBinding::Unknown && index != UINT32_MAX;
        }
    };

    struct BindlessConfig
    {
        uint32_t maxTexture2DCount = 16384;
        uint32_t maxTexture2DArrayCount = 64;
        uint32_t maxTexture3DCount = 64;
        uint32_t maxTextureCubeCount = 64;
        uint32_t maxSamplerCount = 64;
    };

    class GlobalBindings;

    class BindlessRegistry
    {
        friend class GlobalBindings;
    public:
        BindlessRegistry(bool enabled, const BindlessConfig& bindlessConfig);

        ~BindlessRegistry() noexcept = default;

        const BindlessConfig& getConfig() const noexcept
        {
            return mBindlessConfig;
        }

        BindlessHandle registerTexture2D(rhi::TextureView* view) noexcept;
        BindlessHandle registerTexture2DArray(rhi::TextureView* view) noexcept;
        BindlessHandle registerTexture3D(rhi::TextureView* view) noexcept;
        BindlessHandle registerTextureCube(rhi::TextureView* view) noexcept;
        BindlessHandle registerSampler(rhi::Sampler* sampler) noexcept;

        void updateTexture2D(BindlessHandle handle, rhi::TextureView* view);
        void updateTexture2DArray(BindlessHandle handle, rhi::TextureView* view);
        void updateTexture3D(BindlessHandle handle, rhi::TextureView* view);
        void updateTextureCube(BindlessHandle handle, rhi::TextureView* view);
        void updateSampler(BindlessHandle handle, rhi::Sampler* sampler);

        void unregister(BindlessHandle handle);

    private:
        void setGlobalBindings(GlobalBindings* globalBindings) noexcept;

        class IndexPool
        {
        public:
            IndexPool(uint32_t capacity);

            uint32_t acquire() noexcept;

            void release(uint32_t index) noexcept;

            size_t freeCount() const noexcept;

        private:
            std::vector<uint32_t> mFreeIndices;
        };

    private:
        bool mBindlessEnabled;
        BindlessConfig mBindlessConfig;
        GlobalBindings* mGlobalBindings;

        IndexPool mTexture2DPool;
        IndexPool mTexture2DArrayPool;
        IndexPool mTexture3DPool;
        IndexPool mTextureCubePool;
        IndexPool mSamplerPool;
    };
}
