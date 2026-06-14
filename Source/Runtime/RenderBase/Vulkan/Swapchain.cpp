#include "Swapchain.h"
#include "Queue.h"

namespace hyp::vk
{
    Swapchain::Swapchain(const Device& device, const Surface& surface) :
        mDevice(device),
        mSurface(surface)
    {
        const PhysicalDevice& physicalDevice = mDevice.getPhysicalDevice();
        SupportDetails supportDetails = querySwapchainSupport(
            physicalDevice.getHandle(),
            mSurface.getHandle()
        );

        mSurfaceFormat = chooseSwapSuraceFormat(supportDetails);
        mPresentMode = chooseSwapPresentMode(supportDetails);
        mExtent = chooseSwapExtent(supportDetails);
        mImageCount = supportDetails.capabilities.minImageCount + 1;
        if (supportDetails.capabilities.maxImageCount > 0 &&
            mImageCount > supportDetails.capabilities.maxImageCount)
        {
            mImageCount = supportDetails.capabilities.maxImageCount;
        }

        createSwapchain(supportDetails.capabilities);
    }

    Swapchain::~Swapchain()
    {
        mSwapchainImageViews.clear();
        mSwapchainImages.clear();
        vkDestroySwapchainKHR(mDevice.getHandle(), mSwapchain, nullptr);
    }

    VkResult Swapchain::acquireNextImage(uint32_t flightIndex, uint32_t* nextImageIndex, uint64_t timeout)
    {
        VkResult result = vkAcquireNextImageKHR(
            mDevice.getHandle(),
            mSwapchain,
            timeout,
            mImageAvailableSemaphores[flightIndex]->getHandle(),
            VK_NULL_HANDLE,
            &mCurrentImageIndex
        );

        if (nextImageIndex)
            *nextImageIndex = mCurrentImageIndex;

        return result;
    }

    void Swapchain::resize()
    {
        mSwapchainImageViews.clear();
        mSwapchainImages.clear();

        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mDevice.getPhysicalDevice().getHandle(), mSurface.getHandle(), &capabilities);

        mExtent = capabilities.currentExtent;

        VkSwapchainKHR oldSwapchain = mSwapchain;
        createSwapchain(capabilities);
        vkDestroySwapchainKHR(mDevice.getHandle(), oldSwapchain, nullptr);
    }

    void Swapchain::createSwapchain(const VkSurfaceCapabilitiesKHR& capabilities)
    {
        uint32_t graphicsQueueFamilyIndex = mDevice.getGraphicsQueue().getFamilyIndex();
        uint32_t presentQueueFamilyIndex = mDevice.getPresentQueue().getFamilyIndex();
        uint32_t queueFamilyIndices[] = { graphicsQueueFamilyIndex,  presentQueueFamilyIndex };

        VkSwapchainCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = mSurface.getHandle();
        createInfo.minImageCount = mImageCount;
        createInfo.imageFormat = mSurfaceFormat.format;
        createInfo.imageColorSpace = mSurfaceFormat.colorSpace;
        createInfo.imageExtent = mExtent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (graphicsQueueFamilyIndex != presentQueueFamilyIndex)
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        else
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0; // Optional
            createInfo.pQueueFamilyIndices = nullptr; // Optional
        }
        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = mPresentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = mSwapchain;

        vkCreateSwapchainKHR(mDevice.getHandle(), &createInfo, nullptr, &mSwapchain);

        vkGetSwapchainImagesKHR(mDevice.getHandle(), mSwapchain, &mImageCount, nullptr);
        std::vector<VkImage> swapchainImages(mImageCount);
        vkGetSwapchainImagesKHR(mDevice.getHandle(), mSwapchain, &mImageCount, swapchainImages.data());

        mSwapchainImages.resize(mImageCount);
        mSwapchainImageViews.resize(mImageCount);
        for (size_t i = 0; i < mImageCount; i++)
        {
            mSwapchainImages[i] = new Image(
                mDevice,
                swapchainImages[i],
                VK_IMAGE_TYPE_2D,
                mSurfaceFormat.format,
                VkExtent3D{ mExtent.width, mExtent.height, 1 },
                1, 1, 1
            );

            Image::ViewInfo imageViewInfo(*mSwapchainImages[i]);
            mSwapchainImageViews[i] = mSwapchainImages[i]->createView(imageViewInfo);
        }

        uint32_t maxInFlightFrameCount = std::max(mImageCount - 1, 1u);
        mImageAvailableSemaphores.resize(maxInFlightFrameCount);
        for (size_t i = 0; i < maxInFlightFrameCount; i++)
        {
            mImageAvailableSemaphores[i] = new BinarySemaphore(mDevice);
        }
    }

    Swapchain::SupportDetails Swapchain::querySwapchainSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
    {
        SupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &details.capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, details.formats.data());

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);

        if (presentModeCount != 0) {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, details.presentModes.data());
        }

        return details;
    }

    VkSurfaceFormatKHR Swapchain::chooseSwapSuraceFormat(const SupportDetails& supportDetails)
    {
        const auto& availableFormats = supportDetails.formats;

        if (availableFormats.size() == 1 &&
            availableFormats[0].format == VK_FORMAT_UNDEFINED)
        {
            return { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
        }

        for (const auto& availableFormat : availableFormats)
        {
            if (availableFormat.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 &&
                availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return availableFormat;
            }
        }

        for (const auto& availableFormat : availableFormats)
        {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM &&
                availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return availableFormat;
            }
        }

        return availableFormats[0];
    }

    VkPresentModeKHR Swapchain::chooseSwapPresentMode(const SupportDetails& supportDetails)
    {
        const auto& availablePresentModes = supportDetails.presentModes;

        VkPresentModeKHR bestMode = VK_PRESENT_MODE_FIFO_KHR;

        for (const auto& availablePresentMode : availablePresentModes)
        {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return availablePresentMode;
            }
            else if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR)
            {
                bestMode = availablePresentMode;
            }
        }

        return bestMode;
    }

    VkExtent2D Swapchain::chooseSwapExtent(const SupportDetails& supportDetails)
    {
        if (supportDetails.capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return supportDetails.capabilities.currentExtent;
        }
        else
        {
            static constexpr uint32_t kWidth = 1024, kHeight = 768;
            VkExtent2D actualExtent = { kWidth, kHeight };

            actualExtent.width = std::max(supportDetails.capabilities.minImageExtent.width, std::min(supportDetails.capabilities.maxImageExtent.width, actualExtent.width));
            actualExtent.height = std::max(supportDetails.capabilities.minImageExtent.height, std::min(supportDetails.capabilities.maxImageExtent.height, actualExtent.height));

            return actualExtent;
        }
    }
}


