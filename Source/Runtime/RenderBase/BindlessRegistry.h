#pragma once
#include "Vulkan/Image.h"
#include "Vulkan/Sampler.h"

namespace hyp
{
    enum class BindlessBinding : uint32_t
    {
        Unknown = UINT32_MAX,
        Texture2D = 0,
        Texture2DArray,
        TextureCube,
        TextureCubeArray,
        Texture3D,
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
        uint32_t maxTexture2DArrayCount = 8;
        uint32_t maxTextureCubeCount = 8;
#if SUPPORT_TEXTURE_CUBE_ARRAY
        uint32_t maxTextureCubeArrayCount = 8;
#endif
        uint32_t maxTexture3DCount = 8;
        uint32_t maxSamplerCount = 64;
    };

    class GlobalBindings;
    class BindlessRegistry
    {
        friend class GlobalBindings;
    public:
        BindlessRegistry(const BindlessConfig& bindlessConfig);

        ~BindlessRegistry() noexcept = default;

        const BindlessConfig& getBindlessConfig() const noexcept
        {
            return mBindlessConfig;
        }

        BindlessHandle registerTexture2D(VkImageView imageView) noexcept;

        BindlessHandle registerTexture2DArray(VkImageView imageView) noexcept;

        BindlessHandle registerTextureCube(VkImageView imageView) noexcept;

#if SUPPORT_TEXTURE_CUBE_ARRAY
        BindlessHandle registerTextureCubeArray(VkImageView imageView) noexcept;
#endif

        BindlessHandle registerTexture3D(VkImageView imageView) noexcept;

        BindlessHandle registerSampler(VkSampler sampler) noexcept;

        void updateTexture2D(BindlessHandle handle, VkImageView imageView);

        void updateTexture2DArray(BindlessHandle handle, VkImageView imageView);

        void updateTextureCube(BindlessHandle handle, VkImageView imageView);

#if SUPPORT_TEXTURE_CUBE_ARRAY
        void updateTextureCubeArray(BindlessHandle handle, VkImageView imageView);
#endif

        void updateTexture3D(BindlessHandle handle, VkImageView imageView);

        void updateSampler(BindlessHandle handle, VkSampler sampler);

        void unregister(BindlessHandle handle);

    private:
        class IndexPool
        {
        public:
            IndexPool(uint32_t capacity);

            uint32_t acquire() noexcept;

            void release(uint32_t index) noexcept;

            size_t freeCount() const noexcept;

        private:
            std::vector<uint32_t> mFreeIndices;
            // TODO: 可以在Debug模式下使用bitset检查是否重复释放, 但我们需要一个动态的bitset
        };

        void setGlobalBindings(GlobalBindings* globalBindings) noexcept;

    private:
        BindlessConfig mBindlessConfig;
        GlobalBindings* mGlobalBindings;

        IndexPool mTexture2DPool;
        IndexPool mTexture2DArrayPool;
        IndexPool mTextureCubePool;
#if SUPPORT_TEXTURE_CUBE_ARRAY
        IndexPool mTextureCubeArrayPool;
#endif
        IndexPool mTexture3DPool;
        IndexPool mSamplerPool;
    };
}
