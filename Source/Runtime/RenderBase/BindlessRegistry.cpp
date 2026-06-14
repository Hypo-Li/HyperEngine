#include "BindlessRegistry.h"
#include "GlobalBindings.h"

namespace hyp
{
    BindlessRegistry::BindlessRegistry(const BindlessConfig& bindlessConfig) :
        mGlobalBindings(nullptr),
        mTexture2DPool(bindlessConfig.maxTexture2DCount),
        mTexture2DArrayPool(bindlessConfig.maxTexture2DArrayCount),
        mTextureCubePool(bindlessConfig.maxTextureCubeCount),
#if SUPPORT_TEXTURE_CUBE_ARRAY
        mTextureCubeArrayPool(bindlessConfig.maxTextureCubeArrayCount),
#endif
        mTexture3DPool(bindlessConfig.maxTexture3DCount),
        mSamplerPool(bindlessConfig.maxSamplerCount)
    {
        
    }

    BindlessHandle BindlessRegistry::registerTexture2D(VkImageView imageView) noexcept
    {
        uint32_t index = mTexture2DPool.acquire();
        mGlobalBindings->writeTexture2D(index, imageView);
        return BindlessHandle{ BindlessBinding::Texture2D, index };
    }

    BindlessHandle BindlessRegistry::registerTexture2DArray(VkImageView imageView) noexcept
    {
        uint32_t index = mTexture2DArrayPool.acquire();
        mGlobalBindings->writeTexture2DArray(index, imageView);
        return BindlessHandle{ BindlessBinding::Texture2DArray, index };
    }

    BindlessHandle BindlessRegistry::registerTextureCube(VkImageView imageView) noexcept
    {
        uint32_t index = mTextureCubePool.acquire();
        mGlobalBindings->writeTextureCube(index, imageView);
        return BindlessHandle{ BindlessBinding::TextureCube, index };
    }

#if SUPPORT_TEXTURE_CUBE_ARRAY
    BindlessHandle BindlessRegistry::registerTextureCubeArray(VkImageView imageView) noexcept
    {
        uint32_t index = mTextureCubeArrayPool.acquire();
        mGlobalBindings->writeTextureCubeArray(index, imageView);
        return BindlessHandle{ BindlessBinding::TextureCubeArray, index };
    }
#endif

    BindlessHandle BindlessRegistry::registerTexture3D(VkImageView imageView) noexcept
    {
        uint32_t index = mTexture3DPool.acquire();
        mGlobalBindings->writeTexture3D(index, imageView);
        return BindlessHandle{ BindlessBinding::Texture3D, index };
    }

    BindlessHandle BindlessRegistry::registerSampler(VkSampler sampler) noexcept
    {
        uint32_t index = mSamplerPool.acquire();
        mGlobalBindings->writeSampler(index, sampler);
        return BindlessHandle{ BindlessBinding::Sampler, index };
    }

    void BindlessRegistry::updateTexture2D(BindlessHandle handle, VkImageView imageView)
    {
        assert(handle.binding == BindlessBinding::Texture2D);
        mGlobalBindings->writeTexture2D(handle.index, imageView);
    }

    void BindlessRegistry::updateTexture2DArray(BindlessHandle handle, VkImageView imageView)
    {
        assert(handle.binding == BindlessBinding::Texture2DArray);
        mGlobalBindings->writeTexture2DArray(handle.index, imageView);
    }

    void BindlessRegistry::updateTextureCube(BindlessHandle handle, VkImageView imageView)
    {
        assert(handle.binding == BindlessBinding::TextureCube);
        mGlobalBindings->writeTextureCube(handle.index, imageView);
    }

#if SUPPORT_TEXTURE_CUBE_ARRAY
    void BindlessRegistry::updateTextureCubeArray(BindlessHandle handle, VkImageView imageView)
    {
        assert(handle.binding == BindlessBinding::TextureCubeArray);
        mGlobalBindings->writeTextureCubeArray(handle.index, imageView);
    }
#endif

    void BindlessRegistry::updateTexture3D(BindlessHandle handle, VkImageView imageView)
    {
        assert(handle.binding == BindlessBinding::Texture3D);
        mGlobalBindings->writeTexture3D(handle.index, imageView);
    }

    void BindlessRegistry::updateSampler(BindlessHandle handle, VkSampler sampler)
    {
        assert(handle.binding == BindlessBinding::Sampler);
        mGlobalBindings->writeSampler(handle.index, sampler);
    }

    void BindlessRegistry::unregister(BindlessHandle handle)
    {
        uint32_t binding = uint32_t(handle.binding);
        switch (handle.binding)
        {
        case BindlessBinding::Texture2D:
        {
            mTexture2DPool.release(handle.index);
            mGlobalBindings->writeTexture2D(handle.index, VK_NULL_HANDLE);
            break;
        }
        case BindlessBinding::Texture2DArray:
        {
            mTexture2DArrayPool.release(handle.index);
            mGlobalBindings->writeTexture2DArray(handle.index, VK_NULL_HANDLE);
            break;
        }
        case BindlessBinding::TextureCube:
        {
            mTextureCubePool.release(handle.index);
            mGlobalBindings->writeTextureCube(handle.index, VK_NULL_HANDLE);
            break;
        }
#if SUPPORT_TEXTURE_CUBE_ARRAY
        case BindlessBinding::TextureCubeArray:
        {
            mTextureCubeArrayPool.release(handle.index);
            mGlobalBindings.writeTextureCubeArray(handle.index, VK_NULL_HANDLE);
            break;
        }
#endif
        case BindlessBinding::Texture3D:
        {
            mTexture3DPool.release(handle.index);
            mGlobalBindings->writeTexture3D(handle.index, VK_NULL_HANDLE);
            break;
        }
        case BindlessBinding::Sampler:
        {
            mSamplerPool.release(handle.index);
            mGlobalBindings->writeSampler(handle.index, VK_NULL_HANDLE);
            break;
        }
        default:
            break;
        }
    }

    BindlessRegistry::IndexPool::IndexPool(uint32_t capacity)
    {
        mFreeIndices.reserve(capacity);
        for (uint32_t i = 1; i <= capacity; ++i)
        {
            mFreeIndices.push_back(capacity - i);
        }
    }

    uint32_t BindlessRegistry::IndexPool::acquire() noexcept
    {
        if (mFreeIndices.empty())
        {
            LOG_ERROR("IndexPool: No more free indices");
            return UINT32_MAX;
        }
        uint32_t index = mFreeIndices.back();
        mFreeIndices.pop_back();
        return index;
    }

    void BindlessRegistry::IndexPool::release(uint32_t index) noexcept
    {
        mFreeIndices.push_back(index);
    }

    size_t BindlessRegistry::IndexPool::freeCount() const noexcept
    {
        return mFreeIndices.size();
    }

    void BindlessRegistry::setGlobalBindings(GlobalBindings* globalBindings) noexcept
    {
        mGlobalBindings = globalBindings;
    }

}
