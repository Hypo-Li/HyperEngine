#include "Swapchain.h"
#include "Device.h"
#ifdef WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace hyp::vk
{
    static VkSurfaceKHR createSurface(VkInstance instance, const rhi::NativeWindowInfo& nativeWindow)
    {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
#ifdef WIN32
        VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.hinstance = (HINSTANCE)nativeWindow.win32.hinstance;
        surfaceInfo.hwnd = (HWND)nativeWindow.win32.hwnd;
        vkCreateWin32SurfaceKHR(instance, &surfaceInfo, nullptr, &surface);
#else
        // Linux 下 Xlib/Wayland 的宏(VK_USE_PLATFORM_XLIB_KHR/VK_USE_PLATFORM_WAYLAND_KHR)是同时定义的
        // (见 ThirdParty/volk/CMakeLists.txt),实际用哪个是运行时才知道的,所以这里按 nativeWindow.type 分支
        if (nativeWindow.type == rhi::NativeWindowType::Xlib)
        {
#ifdef VK_USE_PLATFORM_XLIB_KHR
            VkXlibSurfaceCreateInfoKHR surfaceInfo = {};
            surfaceInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
            surfaceInfo.dpy = (Display*)nativeWindow.xlib.display;
            // ::Window 显式取全局命名空间下 X11 的 Window(XID)类型,避免和 hyp::Window 这个类同名冲突
            surfaceInfo.window = (::Window)nativeWindow.xlib.window;
            vkCreateXlibSurfaceKHR(instance, &surfaceInfo, nullptr, &surface);
#endif
        }
        else if (nativeWindow.type == rhi::NativeWindowType::Wayland)
        {
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
            VkWaylandSurfaceCreateInfoKHR surfaceInfo = {};
            surfaceInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
            surfaceInfo.display = (struct wl_display*)nativeWindow.wayland.display;
            surfaceInfo.surface = (struct wl_surface*)nativeWindow.wayland.surface;
            vkCreateWaylandSurfaceKHR(instance, &surfaceInfo, nullptr, &surface);
#endif
        }
#endif
        return surface;
    }

    struct SwapchainSupportDetails
    {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };
    static SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
    {
        SwapchainSupportDetails details;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &details.capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, details.formats.data());

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);

        if (presentModeCount != 0)
        {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, details.presentModes.data());
        }

        return details;
    }

    static VkSurfaceFormatKHR chooseSwapchainSurfaceFormat(std::span<VkSurfaceFormatKHR> availableFormats)
    {
        VkSurfaceFormatKHR selectedFormat = availableFormats[0];
        std::vector<VkFormat> preferredImageFormats = {
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_A8B8G8R8_UNORM_PACK32
        };
        for (auto& availableFormat : availableFormats) {
            if (std::find(
                preferredImageFormats.begin(),
                preferredImageFormats.end(),
                availableFormat.format) != preferredImageFormats.end())
            {
                selectedFormat = availableFormat;
                break;
            }
        }

        return selectedFormat;
    }

    static VkPresentModeKHR chooseSwapchainPresentMode(std::span<VkPresentModeKHR> availablePresentModes, bool vsync)
    {
        VkPresentModeKHR bestMode = VK_PRESENT_MODE_FIFO_KHR;

        if (!vsync)
        {
            for (const auto& availablePresentMode : availablePresentModes)
            {
                if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
                {
                    bestMode = availablePresentMode;
                    break;
                }
                else if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR)
                {
                    bestMode = availablePresentMode;
                }
            }
        }

        return bestMode;
    }

    static VkExtent2D chooseSwapchainExtent(const VkSurfaceCapabilitiesKHR& capabilities)
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }
        else
        {
            static constexpr uint32_t kWidth = 1024, kHeight = 768;
            VkExtent2D actualExtent = { kWidth, kHeight };
            actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
            actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));

            return actualExtent;
        }
    }

    rhi::TextureFormat convertFromVkSurfaceFormat(VkSurfaceFormatKHR surfaceFormat)
    {
        switch (surfaceFormat.format)
        {
        case VK_FORMAT_B8G8R8A8_UNORM:
            return rhi::TextureFormat::BGRA8_UNORM;
        case VK_FORMAT_B8G8R8A8_SRGB:
            return rhi::TextureFormat::BGRA8_SRGB;
        case VK_FORMAT_R8G8B8A8_UNORM:
            return rhi::TextureFormat::RGBA8_UNORM;
        case VK_FORMAT_R8G8B8A8_SRGB:
            return rhi::TextureFormat::RGBA8_SRGB;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
            return rhi::TextureFormat::A2BGR10_UNORM_PACK32;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return rhi::TextureFormat::RGBA16_SFLOAT;
        default:
            return rhi::TextureFormat::Undefined;
        }
    }

    rhi::SwapchainStatus convertToSwapchainStatus(VkResult result)
    {
        switch (result)
        {
        case VK_SUCCESS: return rhi::SwapchainStatus::Success;
        case VK_SUBOPTIMAL_KHR: return rhi::SwapchainStatus::Suboptimal;
        case VK_ERROR_OUT_OF_DATE_KHR: return rhi::SwapchainStatus::Outdated;
        case VK_ERROR_SURFACE_LOST_KHR: return rhi::SwapchainStatus::Lost;
        default: return rhi::SwapchainStatus::Error;
        }
    }

    Swapchain::Swapchain(const Device& device, const rhi::SwapchainDesc& desc) :
        rhi::Swapchain(device)
    {
        mSurface = createSurface(device.getInstance(), desc.nativeWindow);
        VkPhysicalDevice physicalDevice = device.getPhysicalDevice();
        auto supportDetails = querySwapchainSupport(physicalDevice, mSurface);
        mSurfaceFormat = chooseSwapchainSurfaceFormat(supportDetails.formats);
        mPresentMode = chooseSwapchainPresentMode(supportDetails.presentModes, desc.vsync);
        mExtent = chooseSwapchainExtent(supportDetails.capabilities);
        mImageCount = supportDetails.capabilities.minImageCount + 1;
        if (supportDetails.capabilities.maxImageCount > 0 &&
            mImageCount > supportDetails.capabilities.maxImageCount)
        {
            mImageCount = supportDetails.capabilities.maxImageCount;
        }

        createSwapchain(supportDetails.capabilities);

        VkSemaphoreCreateInfo semaphoreInfo = {};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (uint32_t i = 0; i < 2; ++i)
        {
            vkCreateSemaphore(device.getHandle(), &semaphoreInfo, nullptr, &mImageAvailableSemaphores[i]);
        }
        mRenderFinishedSemaphores.resize(mImageCount);
        for (uint32_t i = 0; i < mImageCount; ++i)
        {
            vkCreateSemaphore(device.getHandle(), &semaphoreInfo, nullptr, &mRenderFinishedSemaphores[i]);
        }
    }

    Swapchain::~Swapchain()
    {
        auto& device = getVkDevice();
        for (uint32_t i = 0; i < 2; ++i)
        {
            vkDestroySemaphore(device.getHandle(), mImageAvailableSemaphores[i], nullptr);
        }
        for (uint32_t i = 0; i < mImageCount; ++i)
        {
            vkDestroySemaphore(device.getHandle(), mRenderFinishedSemaphores[i], nullptr);
        }
        vkDestroySwapchainKHR(device.getHandle(), mSwapchain, nullptr);
        vkDestroySurfaceKHR(device.getInstance(), mSurface, nullptr);
    }

    rhi::SwapchainStatus Swapchain::acquireNextTexture()
    {
        VkResult result = vkAcquireNextImageKHR(
            getVkDevice().getHandle(),
            mSwapchain,
            UINT64_MAX,
            mImageAvailableSemaphores[mFlightIndex],
            VK_NULL_HANDLE,
            &mCurrentImageIndex
        );
        if (result == VK_SUCCESS)
        {
            mCurrentImageAvailableSemaphoreIndex = mFlightIndex;
            mFlightIndex = (mFlightIndex + 1) % 2;
        }
        return convertToSwapchainStatus(result);
    }

    rhi::TextureView* Swapchain::getCurrentTextureView() const
    {
        return mSwapchainTextureViews[mCurrentImageIndex];
    }

    rhi::TextureFormat Swapchain::getSwapchainTextureFormat() const
    {
        return convertFromVkSurfaceFormat(mSurfaceFormat);
    }

    uint2 Swapchain::getExtent() const
    {
        return uint2(mExtent.width, mExtent.height);
    }

    void Swapchain::resize(uint32_t width, uint32_t height)
    {
        mSwapchainTextureViews.clear();
        mSwapchainTextures.clear();

        auto& device = getVkDevice();
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.getPhysicalDevice(), mSurface, &capabilities);

        if (capabilities.currentExtent.width == 0 || capabilities.currentExtent.height == 0)
        {
            mExtent = capabilities.currentExtent;
            return;
        }

        mExtent.width = std::max(capabilities.minImageExtent.width,
                                 std::min(capabilities.maxImageExtent.width, width));
        mExtent.height = std::max(capabilities.minImageExtent.height,
                                  std::min(capabilities.maxImageExtent.height, height));

        VkSwapchainKHR oldSwapchain = mSwapchain;
        createSwapchain(capabilities);
        vkDestroySwapchainKHR(device.getHandle(), oldSwapchain, nullptr);
    }

    rhi::SwapchainStatus Swapchain::present()
    {
        rhi::SwapchainStatus status = rhi::SwapchainStatus::Success;

        VkSemaphore waitSemaphores[] = {
            mRenderFinishedSemaphores[mCurrentImageIndex]
        };

        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = static_cast<uint32_t>(std::size(waitSemaphores));
        presentInfo.pWaitSemaphores = waitSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &mSwapchain;
        presentInfo.pImageIndices = &mCurrentImageIndex;
        presentInfo.pResults = nullptr;

        VkQueue graphicsQueue = getVkDevice().getGraphicsQueueInfo().queue;
        VkResult result = vkQueuePresentKHR(graphicsQueue, &presentInfo);
        return convertToSwapchainStatus(result);
    }

    void Swapchain::createSwapchain(const VkSurfaceCapabilitiesKHR& capabilities)
    {
        auto& device = getVkDevice();

        VkSwapchainCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = mSurface;
        createInfo.minImageCount = mImageCount;
        createInfo.imageFormat = mSurfaceFormat.format;
        createInfo.imageColorSpace = mSurfaceFormat.colorSpace;
        createInfo.imageExtent = mExtent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = mPresentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = mSwapchain;

        vkCreateSwapchainKHR(device.getHandle(), &createInfo, nullptr, &mSwapchain);
        
        vkGetSwapchainImagesKHR(device.getHandle(), mSwapchain, &mImageCount, nullptr);
        std::vector<VkImage> swapchainImages(mImageCount);
        vkGetSwapchainImagesKHR(device.getHandle(), mSwapchain, &mImageCount, swapchainImages.data());

        mSwapchainTextures.resize(mImageCount);
        mSwapchainTextureViews.resize(mImageCount);
        rhi::TextureDesc swapchainTextureDesc;
        swapchainTextureDesc.type = rhi::TextureType::Texture_2D;
        swapchainTextureDesc.format = convertFromVkSurfaceFormat(mSurfaceFormat);
        swapchainTextureDesc.extent = uint3(mExtent.width, mExtent.height, 1);
        swapchainTextureDesc.mipCount = 1;
        swapchainTextureDesc.layerCount = 1;
        swapchainTextureDesc.samples = 1;
        rhi::TextureViewDesc swapchainTextureViewDesc;
        swapchainTextureViewDesc.type = rhi::TextureViewType::Texture_View_2D;
        swapchainTextureViewDesc.format = swapchainTextureDesc.format;
        swapchainTextureViewDesc.range.aspect = rhi::TextureAspect::Color;
        swapchainTextureViewDesc.range.baseMip = 0;
        swapchainTextureViewDesc.range.mipCount = 1;
        swapchainTextureViewDesc.range.baseLayer = 0;
        swapchainTextureViewDesc.range.layerCount = 1;
        for (uint32_t i = 0; i < mImageCount; ++i)
        {
            mSwapchainTextures[i] = new Texture(
                device,
                swapchainTextureDesc,
                rhi::MemoryType::Gpu_Only,
                swapchainImages[i]
            );
            mSwapchainTextureViews[i] = TRef<TextureView>(
                static_cast<TextureView*>(mSwapchainTextures[i]->createView(swapchainTextureViewDesc).get())
            );
        }
    }

}
