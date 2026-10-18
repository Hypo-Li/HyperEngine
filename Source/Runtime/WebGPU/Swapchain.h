#pragma once
#include "Device.h"
#include "Texture.h"

#include <RHI/Swapchain.h>

namespace hyp::wgpu
{
    class Swapchain : public rhi::Swapchain
    {
    public:
        Swapchain(const Device& device, const rhi::SwapchainDesc& desc);

        ~Swapchain() override;

        rhi::SwapchainStatus acquireNextTexture() override;

        rhi::TextureView* getCurrentTextureView() const override;

        rhi::TextureFormat getSurfaceTextureFormat() const override;

        uint2 getExtent() const override;

        void resize(uint32_t width, uint32_t height) override;

        rhi::SwapchainStatus present() override;

        const Device& getWgpuDevice() const noexcept
        {
            return static_cast<const Device&>(getDevice());
        }

        WGPUSurface getHandle() const noexcept
        {
            return mSurface;
        }

        static WGPUSurface createSurface(WGPUInstance instance, const rhi::NativeWindowInfo& nativeWindow);

    private:
        void configure(uint32_t width, uint32_t height);

        static WGPUPresentMode chooseSwapchainPresentMode(const WGPUSurfaceCapabilities& caps, bool vsync) noexcept;
        static WGPUTextureFormat chooseSwapchainFormat(const WGPUSurfaceCapabilities& caps) noexcept;

    private:
        WGPUSurface mSurface = nullptr;
        WGPUTextureFormat mSurfaceFormat = WGPUTextureFormat_Undefined;
        WGPUPresentMode mPresentMode = WGPUPresentMode_Fifo;
        uint2 mExtent = { 0, 0 };

        TRef<Texture> mCurrentTexture;
        TRef<TextureView> mCurrentTextureView;
    };
}
