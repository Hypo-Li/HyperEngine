#pragma once
#include "ResourceProxy.h"
#include "BindlessRegistry.h"
#include "StagingAllocator.h"

#include <RHI/Texture.h>
#include <RHI/CommandList.h>

namespace hyp
{
    class TextureProxy : public ResourceProxy
    {
    public:
        TextureProxy(const rhi::TextureDesc& desc);

        ~TextureProxy() noexcept;

        void stageUpload(std::vector<uint8_t>&& data, uint32_t mipCount, uint32_t baseLayer = 0, uint32_t layerCount = 1);

        bool prepare(rhi::CommandList& cmdList) override;

        rhi::Texture& getTexture() const noexcept
        {
            return *mTexture;
        }

        rhi::TextureView& getTextureView() const noexcept
        {
            return *mTextureView;
        }

        BindlessHandle getBindlessHandle() const noexcept
        {
            return mBindlessHandle;
        }

    private:
        void flushUploads(rhi::CommandList& cmdList);

    private:
        TRef<rhi::Texture> mTexture;
        TRef<rhi::TextureView> mTextureView;
        BindlessHandle mBindlessHandle;

        struct PendingUpload
        {
            rhi::TextureSlice slice;
            std::vector<uint8_t> data;
            std::optional<StagingAllocation> stagingAllocation;
        };
        std::vector<PendingUpload> mPendingUploads;

        bool mInitialized = false;
    };

    template <>
    struct TProxyTraits<ProxyType::Texture>
    {
        using Proxy = TextureProxy;
    };
}
