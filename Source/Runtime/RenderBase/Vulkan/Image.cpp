#include "Image.h"

namespace hyp::vk
{
    ImageView::ImageView(Image& image, VkImageView imageView, const VkImageSubresourceRange& range)
        : mImage(image), mImageView(imageView), mRange(range)
    {
    }

    ImageView::~ImageView()
    {
        if (mImageView != VK_NULL_HANDLE)
        {
            mImage.destroyView(*this);
        }
    }

    Image::Image(
        const Device& device,
        const CreateInfo& createInfo,
        VmaMemoryUsage memoryUsage,
        VmaAllocationCreateFlags allocationFlags
    ) : mDevice(device),
        mImageType(createInfo.imageType),
        mFormat(createInfo.format),
        mExtent(createInfo.extent),
        mMipLevels(createInfo.mipLevels),
        mArrayLayers(createInfo.arrayLayers),
        mSamples(static_cast<uint8_t>(createInfo.samples)),
        mUsage(createInfo.usage),
        mIsCube(createInfo.isCube),
        mIsOwner(true)
    {
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = createInfo.imageType;
        imageInfo.format = createInfo.format;
        imageInfo.extent = createInfo.extent;
        imageInfo.mipLevels = createInfo.mipLevels;
        imageInfo.arrayLayers = createInfo.arrayLayers;
        imageInfo.samples = createInfo.samples;
        imageInfo.tiling = createInfo.tiling;
        imageInfo.usage = createInfo.usage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (createInfo.isCube)
            imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

        if (memoryUsage == VMA_MEMORY_USAGE_UNKNOWN)
        {
            vkCreateImage(mDevice.getHandle(), &imageInfo, nullptr, &mImage);
        }
        else
        {
            VmaAllocationCreateInfo allocationInfo = {
                .flags = allocationFlags,
                .usage = memoryUsage
            };

            VmaAllocation memoryAllocation;
            MemoryAllocator& memoryAllocator = mDevice.getMemoryAllocator();
            vmaCreateImage(memoryAllocator.getHandle(), &imageInfo, &allocationInfo, &mImage, &memoryAllocation, nullptr);
            mMemory = new MemoryAllocation(memoryAllocator, memoryAllocation);
        }
    }

    Image::Image(
        const Device& device,
        VkImage image,
        VkImageType imageType,
        VkFormat format,
        VkExtent3D extent,
        uint32_t mipLevels,
        uint32_t arrayLayers,
        uint8_t samples
    ) :
        mDevice(device),
        mImage(image),
        mImageType(imageType),
        mFormat(format),
        mExtent(extent),
        mMipLevels(mipLevels),
        mArrayLayers(arrayLayers),
        mSamples(samples),
        mUsage(0),
        mIsCube(false),
        mIsOwner(false)
    {
    }

    Image::~Image()
    {
        for (ImageView* view : mViews)
        {
            vkDestroyImageView(mDevice.getHandle(), view->getHandle(), nullptr);
            view->mImageView = VK_NULL_HANDLE;
        }
        mViews.clear();

        if (mImage != VK_NULL_HANDLE && mIsOwner)
        {
            vkDestroyImage(mDevice.getHandle(), mImage, nullptr);
        }
    }

    bool Image::bindMemory(MemoryAllocation* memory)
    {
        if (mMemory == nullptr)
        {
            mMemory = memory;
            return mMemory->bindImage(mImage);
        }
        return false;
    }

    Image::ViewInfo::ViewInfo()
    {
        viewType = VK_IMAGE_VIEW_TYPE_2D;
        format = VK_FORMAT_UNDEFINED;
        components = {};
        subresourceRange = {};
    }

    Image::ViewInfo::ViewInfo(const Image& image)
    {
        uint32_t arrayLayers = image.getArrayLayers();
        viewType = VK_IMAGE_VIEW_TYPE_2D;
        switch (image.getType())
        {
        case VK_IMAGE_TYPE_1D:
        {
            viewType = arrayLayers == 1 ?
                VK_IMAGE_VIEW_TYPE_1D : VK_IMAGE_VIEW_TYPE_1D_ARRAY;
            break;
        }
        case VK_IMAGE_TYPE_2D:
        {
            if (image.isCube())
            {
                assert(arrayLayers % 6 == 0);
                viewType = arrayLayers / 6 == 1 ?
                    VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
            }
            else
            {
                viewType = arrayLayers == 1 ?
                    VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            }
            break;
        }
        case VK_IMAGE_TYPE_3D:
            viewType = VK_IMAGE_VIEW_TYPE_3D;
            break;
        default:
            break;
        }
        format = image.getFormat();
        components = {};
        subresourceRange = VkImageSubresourceRange{
            .aspectMask = Image::getAspectMask(image.getFormat()),
            .baseMipLevel = 0,
            .levelCount = image.getMipLevels(),
            .baseArrayLayer = 0,
            .layerCount = image.getArrayLayers()
        };
    }

    ImageView* Image::createView(const ViewInfo& viewInfo)
    {
        VkImageViewCreateInfo viewCreateInfo = {};
        viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCreateInfo.image = mImage;
        viewCreateInfo.viewType = viewInfo.viewType;
        viewCreateInfo.format = viewInfo.format;
        viewCreateInfo.components = viewInfo.components;
        viewCreateInfo.subresourceRange = viewInfo.subresourceRange;

        VkImageView imageView;
        vkCreateImageView(mDevice.getHandle(), &viewCreateInfo, nullptr, &imageView);
        ImageView* newView = new ImageView(*this, imageView, viewInfo.subresourceRange);
        mViews.push_back(newView);
        return newView;
    }

    bool Image::destroyView(ImageView& imageView)
    {
        auto it = std::find(mViews.begin(), mViews.end(), &imageView);
        if (it != mViews.end())
        {
            vkDestroyImageView(mDevice.getHandle(), imageView.getHandle(), nullptr);
            imageView.mImageView = VK_NULL_HANDLE;
            mViews.erase(it);
            return true;
        }
        return false;
    }

    VkImageAspectFlags Image::getAspectMask(VkFormat format) noexcept
    {
        if (isDepthFormat(format))
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        if (isStencilFormat(format))
            return VK_IMAGE_ASPECT_STENCIL_BIT;
        if (isDepthStencilFormat(format))
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }

    bool Image::isDepthFormat(VkFormat format) noexcept
    {
        if (format == VK_FORMAT_D16_UNORM ||
            format == VK_FORMAT_D32_SFLOAT)
            return true;
        return false;
    }

    bool Image::isStencilFormat(VkFormat format) noexcept
    {
        if (format == VK_FORMAT_S8_UINT)
            return true;
        return false;
    }

    bool Image::isDepthStencilFormat(VkFormat format) noexcept
    {
        if (format == VK_FORMAT_D16_UNORM_S8_UINT ||
            format == VK_FORMAT_D24_UNORM_S8_UINT ||
            format == VK_FORMAT_D32_SFLOAT_S8_UINT)
            return true;
        return false;
    }

    bool Image::isColorFormat(VkFormat format) noexcept
    {
        if (isDepthFormat(format) ||
            isStencilFormat(format) ||
            isDepthStencilFormat(format))
            return false;
        return true;
    }

}
