#pragma once
#include "Memory.h"

namespace hyp::vk
{
    class Image;
    class ImageView : public Refable
    {
        friend class Image;
    public:
        ~ImageView();

        Image& getImage() const
        {
            return mImage;
        }

        VkImageView getHandle() const
        {
            return mImageView;
        }

        const VkImageSubresourceRange& getRange() const
        {
            return mRange;
        }

    private:
        ImageView(Image& image, VkImageView imageView, const VkImageSubresourceRange& range);

    private:
        Image& mImage;
        VkImageView mImageView{ VK_NULL_HANDLE };
        VkImageSubresourceRange mRange;
    };

    class Image : public Refable
    {
    public:
        struct CreateInfo
        {
            VkImageType imageType = VK_IMAGE_TYPE_2D;
            VkFormat format = VK_FORMAT_UNDEFINED;
            VkExtent3D extent = { 0, 0, 0 };
            uint32_t mipLevels = 1;
            uint32_t arrayLayers = 1;
            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
            VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
            VkImageUsageFlags usage = 0;
            bool isCube = false;
        };

        Image(
            const Device& device,
            const CreateInfo& createInfo,
            VmaMemoryUsage memoryUsage,
            VmaAllocationCreateFlags allocationFlags = 0
        );

        Image(
            const Device& device,
            VkImage image,
            VkImageType imageType,
            VkFormat format,
            VkExtent3D extent,
            uint32_t mipLevels,
            uint32_t arrayLayers,
            uint8_t samples
        );

        ~Image() noexcept;

        const vk::Device& getDevice() const noexcept
        {
            return mDevice;
        }

        VkImage getHandle() const noexcept
        {
            return mImage;
        }

        VkImageType getType() const noexcept
        {
            return mImageType;
        }

        VkFormat getFormat() const noexcept
        {
            return mFormat;
        }

        const VkExtent3D& getExtent() const noexcept
        {
            return mExtent;
        }

        uint32_t getMipLevels() const noexcept
        {
            return mMipLevels;
        }

        uint32_t getArrayLayers() const noexcept
        {
            return mArrayLayers;
        }

        uint8_t getSamples() const noexcept
        {
            return mSamples;
        }

        VkImageUsageFlags getUsage() const noexcept
        {
            return mUsage;
        }

        bool isCube() const noexcept
        {
            return mIsCube;
        }

        VkMemoryRequirements getMemoryRequirements() const
        {
            VkMemoryRequirements memRequirements;
            vkGetImageMemoryRequirements(mDevice.getHandle(), mImage, &memRequirements);
            return memRequirements;
        }

        bool bindMemory(MemoryAllocation* memory);

        MemoryAllocation* getMemory() const noexcept
        {
            return mMemory;
        }

        struct ViewInfo
        {
            VkImageViewType viewType;
            VkFormat format;
            VkComponentMapping components;
            VkImageSubresourceRange subresourceRange;

            ViewInfo();

            ViewInfo(const Image& image);
        };

        ImageView* createView(const ViewInfo& viewInfo);

        bool destroyView(ImageView& imageView);

        const std::vector<TRef<ImageView>>& getViews() const noexcept
        {
            return mViews;
        }

        static VkImageAspectFlags getAspectMask(VkFormat format) noexcept;

        static bool isDepthFormat(VkFormat format) noexcept;

        static bool isStencilFormat(VkFormat format) noexcept;

        static bool isDepthStencilFormat(VkFormat format) noexcept;

        static bool isColorFormat(VkFormat format) noexcept;

    private:
        const Device& mDevice;
        VkImage mImage{ VK_NULL_HANDLE };
        VkImageType mImageType;
        VkFormat mFormat;
        VkExtent3D mExtent;
        uint32_t mMipLevels;
        uint32_t mArrayLayers;
        uint8_t mSamples;
        VkImageUsageFlags mUsage;
        bool mIsCube : 1;
        bool mIsOwner : 1;
        TRef<MemoryAllocation> mMemory;

        std::vector<TRef<ImageView>> mViews;
    };
}
