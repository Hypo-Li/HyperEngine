#pragma once
#include "Texture.h"

#include <RHI/Swapchain.h>

namespace hyp::vk
{
    class Swapchain : public rhi::Swapchain
    {
    public:
        Swapchain(const Device& device, const rhi::SwapchainDesc& desc);

        ~Swapchain();

        rhi::SwapchainStatus acquireNextTexture() override;

        rhi::TextureView* getCurrentTextureView() const override;

        rhi::TextureFormat getSurfaceTextureFormat() const override;

        uint2 getExtent() const override;

        void resize(uint32_t width, uint32_t height) override;

        rhi::SwapchainStatus present() override;

        const Device& getVkDevice() const noexcept
        {
            return static_cast<const Device&>(getDevice());
        }

        VkSwapchainKHR getHandle() const noexcept
        {
            return mSwapchain;
        }

        VkSemaphore getCurrentImageAvailableSemaphore() const noexcept
        {
            return mImageAvailableSemaphores[mCurrentImageAvailableSemaphoreIndex];
        }

        VkSemaphore getRenderFinishedSemaphore() const noexcept
        {
            return mRenderFinishedSemaphores[mCurrentImageIndex];
        }

    private:
        void createSwapchain(const VkSurfaceCapabilitiesKHR& capabilities);

    private:
        VkSurfaceKHR mSurface = VK_NULL_HANDLE;
        VkSwapchainKHR mSwapchain = VK_NULL_HANDLE;
        VkSurfaceFormatKHR mSurfaceFormat;
        VkPresentModeKHR mPresentMode;
        VkExtent2D mExtent;
        uint32_t mImageCount = 0;
        uint32_t mFlightIndex = 0;
        uint32_t mCurrentImageIndex = 0;
        std::array<VkSemaphore, 2> mImageAvailableSemaphores;
        uint32_t mCurrentImageAvailableSemaphoreIndex = 0;
        std::vector<TRef<Texture>> mSwapchainTextures;
        std::vector<TRef<TextureView>> mSwapchainTextureViews;
        std::vector<VkSemaphore> mRenderFinishedSemaphores;
    };
}
