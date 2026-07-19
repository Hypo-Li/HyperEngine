#include "BindlessRegistry.h"
#include "GlobalBindings.h"

#include <Core/Common/LogSystem.h>

namespace hyp
{
    BindlessRegistry::BindlessRegistry(bool enabled, const BindlessConfig& bindlessConfig) :
        mBindlessEnabled(enabled),
        mBindlessConfig(bindlessConfig),
        mTexture2DPool(bindlessConfig.maxTexture2DCount),
        mTexture2DArrayPool(bindlessConfig.maxTexture2DArrayCount),
        mTexture3DPool(bindlessConfig.maxTexture3DCount),
        mTextureCubePool(bindlessConfig.maxTextureCubeCount),
        mSamplerPool(bindlessConfig.maxSamplerCount)
    {
    }

    BindlessHandle BindlessRegistry::registerTexture2D(rhi::TextureView* view) noexcept
    {
        if (!mBindlessEnabled) return {};
        uint32_t index = mTexture2DPool.acquire();
        mGlobalBindings->writeTexture2D(index, view);
        return BindlessHandle{ BindlessBinding::Texture2D, index };
    }

    BindlessHandle BindlessRegistry::registerTexture2DArray(rhi::TextureView* view) noexcept
    {
        if (!mBindlessEnabled) return {};
        uint32_t index = mTexture2DArrayPool.acquire();
        mGlobalBindings->writeTexture2DArray(index, view);
        return BindlessHandle{ BindlessBinding::Texture2DArray, index };
    }

    BindlessHandle BindlessRegistry::registerTexture3D(rhi::TextureView* view) noexcept
    {
        if (!mBindlessEnabled) return {};
        uint32_t index = mTexture3DPool.acquire();
        mGlobalBindings->writeTexture3D(index, view);
        return BindlessHandle{ BindlessBinding::Texture3D, index };
    }

    BindlessHandle BindlessRegistry::registerTextureCube(rhi::TextureView* view) noexcept
    {
        if (!mBindlessEnabled) return {};
        uint32_t index = mTextureCubePool.acquire();
        mGlobalBindings->writeTextureCube(index, view);
        return BindlessHandle{ BindlessBinding::TextureCube, index };
    }

    BindlessHandle BindlessRegistry::registerSampler(rhi::Sampler* sampler) noexcept
    {
        if (!mBindlessEnabled) return {};
        uint32_t index = mSamplerPool.acquire();
        mGlobalBindings->writeSampler(index, sampler);
        return BindlessHandle{ BindlessBinding::Sampler, index };
    }

    void BindlessRegistry::updateTexture2D(BindlessHandle handle, rhi::TextureView* view)
    {
        if (!mBindlessEnabled) return;
        assert(handle.binding == BindlessBinding::Texture2D);
        mGlobalBindings->writeTexture2D(handle.index, view);
    }

    void BindlessRegistry::updateTexture2DArray(BindlessHandle handle, rhi::TextureView* view)
    {
        if (!mBindlessEnabled) return;
        assert(handle.binding == BindlessBinding::Texture2DArray);
        mGlobalBindings->writeTexture2DArray(handle.index, view);
    }

    void BindlessRegistry::updateTexture3D(BindlessHandle handle, rhi::TextureView* view)
    {
        if (!mBindlessEnabled) return;
        assert(handle.binding == BindlessBinding::Texture3D);
        mGlobalBindings->writeTexture3D(handle.index, view);
    }

    void BindlessRegistry::updateTextureCube(BindlessHandle handle, rhi::TextureView* view)
    {
        if (!mBindlessEnabled) return;
        assert(handle.binding == BindlessBinding::TextureCube);
        mGlobalBindings->writeTextureCube(handle.index, view);
    }

    void BindlessRegistry::updateSampler(BindlessHandle handle, rhi::Sampler* sampler)
    {
        if (!mBindlessEnabled) return;
        assert(handle.binding == BindlessBinding::Sampler);
        mGlobalBindings->writeSampler(handle.index, sampler);
    }

    void BindlessRegistry::unregister(BindlessHandle handle)
    {
        if (!mBindlessEnabled) return;

        switch (handle.binding)
        {
        case BindlessBinding::Texture2D:
        {
            mTexture2DPool.release(handle.index);
            mGlobalBindings->writeTexture2D(handle.index, nullptr);
            break;
        }
        case BindlessBinding::Texture2DArray:
        {
            mTexture2DArrayPool.release(handle.index);
            mGlobalBindings->writeTexture2DArray(handle.index, nullptr);
            break;
        }
        case BindlessBinding::TextureCube:
        {
            mTextureCubePool.release(handle.index);
            mGlobalBindings->writeTextureCube(handle.index, nullptr);
            break;
        }
        case BindlessBinding::Texture3D:
        {
            mTexture3DPool.release(handle.index);
            mGlobalBindings->writeTexture3D(handle.index, nullptr);
            break;
        }
        case BindlessBinding::Sampler:
        {
            mSamplerPool.release(handle.index);
            mGlobalBindings->writeSampler(handle.index, nullptr);
            break;
        }
        default:
            break;
        }
    }

    void BindlessRegistry::setGlobalBindings(GlobalBindings* globalBindings) noexcept
    {
        mGlobalBindings = globalBindings;
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

}
