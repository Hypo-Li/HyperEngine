#pragma once
#include "RenderProxy.h"
#include "BindlessRegistry.h"
#include "Vulkan/Image.h"
#include "Vulkan/CommandBuffer.h"
#include "StagingAllocator.h"

namespace hyp
{
    class TextureProxy : public RenderProxy
    {
    public:
        TextureProxy(const vk::Image::CreateInfo& imageInfo);

        ~TextureProxy() noexcept;

        void stageUpload(std::vector<uint8_t> data, uint32_t mipLevel, uint32_t baseLayer = 0, uint32_t layerCount = 1);

        void prepare(vk::CommandBuffer& cmd);

        // Only for Render Target
        //void resize(uint32_t width, uint32_t height);

        vk::Image& getImage() const noexcept
        {
            return *mImage;
        }

        vk::ImageView& getImageView() const noexcept
        {
            return *mImageView;
        }

        BindlessHandle getBindlessHandle() const noexcept
        {
            return mBindlessHandle;
        }

    private:
        void flushUploads(vk::CommandBuffer& cmd);

    private:
        TRef<vk::Image> mImage;
        TRef<vk::ImageView> mImageView;
        BindlessHandle mBindlessHandle;

        struct PendingUpload
        {
            uint32_t mipLevel;
            uint32_t baseLayer;
            uint32_t layerCount;
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
