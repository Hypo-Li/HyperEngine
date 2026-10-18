#include "TransientResources.h"
#include <RHI/Device.h>

namespace hyp::rg
{
    TransientResources::TransientResources(const rhi::Device& device) :
        mDevice(device)
    {
    }

    TransientResources::~TransientResources()
    {
        mTextureMap.clear();
        for (auto& [texture, subTextureMap] : mSubTextureMaps)
        {
            subTextureMap.clear();
        }
        mSubTextureMaps.clear();
        mOwnedTextures.clear();
    }

    TRef<rhi::Buffer> TransientResources::acquireBuffer(const BufferKey& key, uint32_t maxUnusedFrames)
    {
        TRef<rhi::Buffer> result;
        auto it = mBufferMap.find(key);
        if (it == mBufferMap.end())
        {
            result = createBuffer(mDevice, key);
            mBufferMap.try_emplace(key, result.get(), maxUnusedFrames);
        }
        else
        {
            result = it->second.resource;
        }
        return result;
    }

    TRef<rhi::TextureView> TransientResources::acquireTexture(const TextureKey& key, uint32_t maxUnusedFrames)
    {
        TRef<rhi::TextureView> result;
        auto it = mTextureMap.find(key);
        if (it == mTextureMap.end())
        {
            auto [texture, textureView] = createTexture(mDevice, key);
            // TextureView 对 Texture 只持裸引用, owner 记录的冷却时间必须跟这份 view 记录保持一致,
            // 否则 view 记录还没到期就可能因为 owner 先到期被销毁, 留下悬空指针
            mOwnedTextures.try_emplace(texture.get(), texture.get(), maxUnusedFrames);
            result = textureView;
            mTextureMap.try_emplace(key, result.get(), maxUnusedFrames);
        }
        else
        {
            result = it->second.resource;
        }
        return result;
    }

    TRef<rhi::TextureView> TransientResources::acquireSubTexture(const rhi::TextureView& texture, const SubTextureKey& key)
    {
        rhi::Texture* parentTexture = &texture.getTexture();
        mOwnedTextures.try_emplace(parentTexture, parentTexture, kDefaultMaxUnusedFrames);
        SubTextureMap& subTextureMap = mSubTextureMaps[parentTexture];
        auto subTextureIt = subTextureMap.find(key);
        if (subTextureIt != subTextureMap.end())
        {
            return subTextureIt->second.resource;
        }
        TRef<rhi::TextureView> result = createSubTexture(texture.getTexture(), key);
        subTextureMap.try_emplace(key, result.get(), kDefaultMaxUnusedFrames);
        return result;
    }

    void TransientResources::destroyUselessResources()
    {
        for (auto it = mBufferMap.begin(); it != mBufferMap.end(); )
        {
            auto& record = it->second;
            if (record.shouldDestroy())
            {
                it = mBufferMap.erase(it);
            }
            else
            {
                ++it;
            }
        }

        for (auto it = mTextureMap.begin(); it != mTextureMap.end(); )
        {
            auto& record = it->second;
            if (record.shouldDestroy())
            {
                // 销毁未使用的primary texture view
                it = mTextureMap.erase(it);
            }
            else
            {
                ++it;
            }
        }

        for (auto textureIt = mSubTextureMaps.begin(); textureIt != mSubTextureMaps.end();)
        {
            auto& subTextureMap = textureIt->second;
            for (auto it = subTextureMap.begin(); it != subTextureMap.end();)
            {
                auto& record = it->second;
                if (record.shouldDestroy())
                {
                    it = subTextureMap.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            if (subTextureMap.empty())
                textureIt = mSubTextureMaps.erase(textureIt);
            else
                ++textureIt;
        }

        for (auto it = mOwnedTextures.begin(); it != mOwnedTextures.end();)
        {
            if (it->second.shouldDestroy())
            {
                it = mOwnedTextures.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void TransientResources::clearBarriers()
    {
        mPrePassBufferBarriers.clear();
        mPrePassTextureBarriers.clear();
        mPostPassBufferBarriers.clear();
        mPostPassTextureBarriers.clear();
    }

    TRef<rhi::Buffer> TransientResources::createBuffer(const rhi::Device& device, const BufferKey& key)
    {
        TRef<rhi::Buffer> buffer = device.createBuffer(key.desc, rhi::MemoryType::Gpu_Only);
        buffer->setDebugName(key.name);
        return buffer;
    }

    std::pair<TRef<rhi::Texture>, TRef<rhi::TextureView>> TransientResources::createTexture(const rhi::Device& device, const TextureKey& key)
    {
        TRef<rhi::Texture> texture = device.createTexture(key.desc, rhi::MemoryType::Gpu_Only);
        texture->setDebugName(key.name);

        rhi::TextureViewDesc viewDesc = makeTextureDefaultViewDesc(key.desc);
        TRef<rhi::TextureView> textureDefaultView = texture->createView(viewDesc);

        return { texture, textureDefaultView };
    }

    TRef<rhi::TextureView> TransientResources::createSubTexture(rhi::Texture& texture, const SubTextureKey& key)
    {
        TRef<rhi::TextureView> textureView = texture.createView(key.desc);
        textureView->setDebugName(key.name);

        return textureView;
    }

}

