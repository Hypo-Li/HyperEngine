#pragma once
#include "Device.h"
#include "Surface.h"
#include "Image.h"
#include "Semaphore.h"

namespace hyp::vk
{
    class Swapchain : public Refable
    {
    public:
        Swapchain(const Device& device, const Surface& surface);
        ~Swapchain();

        const Device& getDevice() const noexcept
        {
            return mDevice;
        }

        VkSwapchainKHR getHandle() const noexcept
        {
            return mSwapchain;
        }

        VkSurfaceFormatKHR getSurfaceFormat() const noexcept
        {
            return mSurfaceFormat;
        }

        VkPresentModeKHR getPresentMode() const noexcept
        {
            return mPresentMode;
        }

        VkExtent2D getExtent() const noexcept
        {
            return mExtent;
        }

        uint32_t getImageCount() const noexcept
        {
            return mImageCount;
        }

        Image* getImage(uint32_t imageIndex) const noexcept
        {
            assert(imageIndex < mSwapchainImages.size());
            return mSwapchainImages[imageIndex];
        }

        ImageView* getImageView(uint32_t imageIndex) const noexcept
        {
            assert(imageIndex < mSwapchainImageViews.size());
            return mSwapchainImageViews[imageIndex];
        }

        Semaphore* getImageAvailableSemaphore(uint32_t flightIndex) const noexcept
        {
            assert(flightIndex < mImageAvailableSemaphores.size());
            return mImageAvailableSemaphores[flightIndex];
        }

        // 获取下一张图像的索引, 成功后会设置frameIndex所表示的semaphore和fence
        VkResult acquireNextImage(uint32_t flightIndex, uint32_t* nextImageIndex, uint64_t timeout = std::numeric_limits<uint64_t>::max());

        uint32_t getCurrentImageIndex() const noexcept
        {
            return mCurrentImageIndex;
        }

        void resize();

    private:
        void createSwapchain(const VkSurfaceCapabilitiesKHR& capabilities);

        struct SupportDetails
        {
            VkSurfaceCapabilitiesKHR capabilities;
            std::vector<VkSurfaceFormatKHR> formats;
            std::vector<VkPresentModeKHR> presentModes;
        };

        static SupportDetails querySwapchainSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);

        static VkSurfaceFormatKHR chooseSwapSuraceFormat(const SupportDetails& supportDetails);

        static VkPresentModeKHR chooseSwapPresentMode(const SupportDetails& supportDetails);

        static VkExtent2D chooseSwapExtent(const SupportDetails& supportDetails);

    private:
        const Device& mDevice;
        const Surface& mSurface;
        VkSwapchainKHR mSwapchain{ VK_NULL_HANDLE };
        VkSurfaceFormatKHR mSurfaceFormat;
        VkPresentModeKHR mPresentMode;
        VkExtent2D mExtent;
        uint32_t mImageCount;
        uint32_t mCurrentImageIndex = ~0u;

        std::vector<TRef<Image>> mSwapchainImages;
        std::vector<TRef<ImageView>> mSwapchainImageViews;
        std::vector<TRef<Semaphore>> mImageAvailableSemaphores;
    };
}
