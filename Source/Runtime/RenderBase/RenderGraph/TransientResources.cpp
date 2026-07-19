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
    }

    TRef<rhi::Buffer> TransientResources::acquireBuffer(const BufferKey& key)
    {
        TRef<rhi::Buffer> result;
        auto it = mBufferMap.find(key);
        if (it == mBufferMap.end())
        {
            result = createBuffer(mDevice, key);
            mBufferMap.emplace(key, result);
        }
        else
        {
            result = it->second.resource;
        }
        return result;
    }

    TRef<rhi::TextureView> TransientResources::acquireTexture(const TextureKey& key)
    {
        TRef<rhi::TextureView> result;
        auto it = mTextureMap.find(key);
        if (it == mTextureMap.end())
        {
            auto [texture, textureView] = createTexture(mDevice, key);
            mSubTextureMaps.emplace(texture, SubTextureMap{});
            result = textureView;
            mTextureMap.emplace(key, result);
        }
        else
        {
            result = it->second.resource;
        }
        return result;
    }

    TRef<rhi::TextureView> TransientResources::acquireSubTexture(const rhi::TextureView& texture, const SubTextureKey& key)
    {
        SubTextureMap& subTextureMap = mSubTextureMaps[&texture.getTexture()];
        auto subTextureIt = subTextureMap.find(key);
        if (subTextureIt != subTextureMap.end())
        {
            return subTextureIt->second.resource;
        }
        TRef<rhi::TextureView> result = createSubTexture(texture.getTexture(), key);
        subTextureMap.emplace(key, result);
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

            if (subTextureMap.empty() && textureIt->first->getViewCount() == 0)
                textureIt = mSubTextureMaps.erase(textureIt);
            else
                ++textureIt;
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

