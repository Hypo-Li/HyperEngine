#pragma once
#include "Device.h"

#include <RHI/Texture.h>

namespace hyp::vk
{
    class Texture;
    class TextureView : public rhi::TextureView
    {
    public:
        TextureView(Texture& texture, const rhi::TextureViewDesc& desc);

        ~TextureView();

        const Texture& getVkTexture() const noexcept;

        VkImageView getHandle() const noexcept
        {
            return mImageView;
        }

        void setDebugName(HString name) override;

    private:
        VkImageView mImageView = VK_NULL_HANDLE;
    };

    class Texture : public rhi::Texture
    {
    public:
        Texture(const Device& device, const rhi::TextureDesc& desc, rhi::MemoryType memoryType);

        Texture(const Device& device, const rhi::TextureDesc& desc, rhi::MemoryType memoryType, VkImage image);

        ~Texture();

        const Device& getVkDevice() const noexcept
        {
            return static_cast<const Device&>(getDevice());
        }

        VkImage getHandle() const noexcept
        {
            return mImage;
        }

        TRef<rhi::TextureView> createView(const rhi::TextureViewDesc& desc) override;

        void setDebugName(HString name) override;

    private:
        VkImage mImage = VK_NULL_HANDLE;
        VmaAllocation mAllocation = VK_NULL_HANDLE;
    };
}
