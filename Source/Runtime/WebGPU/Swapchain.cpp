#include "Swapchain.h"

#include <Core/Common/LogSystem.h>

#ifdef WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace hyp::wgpu
{
    static rhi::TextureFormat convertRHISurfaceFormat(WGPUTextureFormat format) noexcept
    {
        switch (format)
        {
        case WGPUTextureFormat_BGRA8Unorm:     return rhi::TextureFormat::BGRA8_UNORM;
        case WGPUTextureFormat_BGRA8UnormSrgb: return rhi::TextureFormat::BGRA8_SRGB;
        case WGPUTextureFormat_RGBA8Unorm:     return rhi::TextureFormat::RGBA8_UNORM;
        case WGPUTextureFormat_RGBA8UnormSrgb: return rhi::TextureFormat::RGBA8_SRGB;
        case WGPUTextureFormat_RGB10A2Unorm:   return rhi::TextureFormat::A2BGR10_UNORM_PACK32;
        case WGPUTextureFormat_RGBA16Float:    return rhi::TextureFormat::RGBA16_SFLOAT;
        default:                               return rhi::TextureFormat::Undefined;
        }
    }

    WGPUSurface Swapchain::createSurface(WGPUInstance instance, const rhi::NativeWindowInfo& nativeWindow)
    {
#ifdef WIN32
        if (nativeWindow.type == rhi::NativeWindowType::Win32)
        {
            WGPUSurfaceSourceWindowsHWND surfaceSrc = WGPU_SURFACE_SOURCE_WINDOWS_HWND_INIT;
            surfaceSrc.hinstance = nativeWindow.win32.hinstance;
            surfaceSrc.hwnd = nativeWindow.win32.hwnd;

            WGPUSurfaceDescriptor surfaceDesc = WGPU_SURFACE_DESCRIPTOR_INIT;
            surfaceDesc.nextInChain = &surfaceSrc.chain;
            surfaceDesc.label = WGPUStringView{ "UnnamedSurface", WGPU_STRLEN };

            return wgpuInstanceCreateSurface(instance, &surfaceDesc);
        }
#elif defined(__linux__)
        if (nativeWindow.type == rhi::NativeWindowType::Xlib)
        {
            WGPUSurfaceSourceXlibWindow surfaceSrc = WGPU_SURFACE_SOURCE_XLIB_WINDOW_INIT;
            surfaceSrc.display = nativeWindow.xlib.display;
            surfaceSrc.window = nativeWindow.xlib.window;

            WGPUSurfaceDescriptor surfaceDesc = WGPU_SURFACE_DESCRIPTOR_INIT;
            surfaceDesc.nextInChain = &surfaceSrc.chain;
            surfaceDesc.label = WGPUStringView{ "UnnamedSurface", WGPU_STRLEN };

            return wgpuInstanceCreateSurface(instance, &surfaceDesc);
        }
        else if (nativeWindow.type == rhi::NativeWindowType::Wayland)
        {
            WGPUSurfaceSourceWaylandSurface surfaceSrc = WGPU_SURFACE_SOURCE_WAYLAND_SURFACE_INIT;
            surfaceSrc.display = nativeWindow.wayland.display;
            surfaceSrc.surface = nativeWindow.wayland.surface;

            WGPUSurfaceDescriptor surfaceDesc = WGPU_SURFACE_DESCRIPTOR_INIT;
            surfaceDesc.nextInChain = &surfaceSrc.chain;
            surfaceDesc.label = WGPUStringView{ "UnnamedSurface", WGPU_STRLEN };

            return wgpuInstanceCreateSurface(instance, &surfaceDesc);
        }
#endif
        LOG_ERROR("Unsupported WebGPU native window type");
        return nullptr;
    }

    WGPUTextureFormat Swapchain::chooseSwapchainFormat(const WGPUSurfaceCapabilities& caps) noexcept
    {
        for (size_t i = 0; i < caps.formatCount; ++i)
        {
            if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm)
                return WGPUTextureFormat_BGRA8Unorm;
        }
        if (caps.formatCount > 0)
            return caps.formats[0];
        return WGPUTextureFormat_BGRA8Unorm;
    }

    WGPUPresentMode Swapchain::chooseSwapchainPresentMode(const WGPUSurfaceCapabilities& caps, bool vsync) noexcept
    {
        if (vsync)
            return WGPUPresentMode_Fifo;

        for (size_t i = 0; i < caps.presentModeCount; ++i)
        {
            if (caps.presentModes[i] == WGPUPresentMode_Mailbox)
                return WGPUPresentMode_Mailbox;
        }
        for (size_t i = 0; i < caps.presentModeCount; ++i)
        {
            if (caps.presentModes[i] == WGPUPresentMode_Immediate)
                return WGPUPresentMode_Immediate;
        }
        return WGPUPresentMode_Fifo;
    }

    Swapchain::Swapchain(const Device& device, const rhi::SwapchainDesc& desc)
        : rhi::Swapchain(device)
    {
        mSurface = createSurface(device.getInstance(), desc.nativeWindow);
        if (!mSurface)
            return;

        WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
        wgpuSurfaceGetCapabilities(mSurface, device.getAdapter(), &caps);

        mSurfaceFormat = chooseSwapchainFormat(caps);
        mPresentMode = chooseSwapchainPresentMode(caps, desc.vsync);

        wgpuSurfaceCapabilitiesFreeMembers(caps);

        configure(desc.width, desc.height);
    }

    Swapchain::~Swapchain()
    {
        if (mSurface)
        {
            wgpuSurfaceUnconfigure(mSurface);
            wgpuSurfaceRelease(mSurface);
            mSurface = nullptr;
        }
    }

    void Swapchain::configure(uint32_t width, uint32_t height)
    {
        mExtent = { width, height };

        WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;
        config.device = getWgpuDevice().getHandle();
        config.format = mSurfaceFormat;
        config.usage = WGPUTextureUsage_RenderAttachment;
        config.width = width;
        config.height = height;
        config.alphaMode = WGPUCompositeAlphaMode_Auto;
        config.presentMode = mPresentMode;

        wgpuSurfaceConfigure(mSurface, &config);
    }

    rhi::SwapchainStatus Swapchain::acquireNextTexture()
    {
        WGPUSurfaceTexture surfaceTexture = WGPU_SURFACE_TEXTURE_INIT;
        wgpuSurfaceGetCurrentTexture(mSurface, &surfaceTexture);

        rhi::SwapchainStatus status;
        switch (surfaceTexture.status)
        {
        case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
            status = rhi::SwapchainStatus::Success;
            break;
        case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
            status = rhi::SwapchainStatus::Suboptimal;
            break;
        case WGPUSurfaceGetCurrentTextureStatus_Outdated:
            status = rhi::SwapchainStatus::Outdated;
            break;
        case WGPUSurfaceGetCurrentTextureStatus_Lost:
            status = rhi::SwapchainStatus::Lost;
            break;
        default:
            status = rhi::SwapchainStatus::Error;
            break;
        }

        if (status != rhi::SwapchainStatus::Success &&
            status != rhi::SwapchainStatus::Suboptimal)
        {
            mCurrentTextureView = nullptr;
            return status;
        }

        mCurrentTextureView = nullptr;

        WGPUTexture handle = surfaceTexture.texture;
        if (!mCurrentTexture)
        {
            rhi::TextureDesc textureDesc;
            textureDesc.type = rhi::TextureType::Texture_2D;
            textureDesc.format = convertRHISurfaceFormat(mSurfaceFormat);
            textureDesc.extent = uint3(mExtent.x, mExtent.y, 1);
            textureDesc.mipCount = 1;
            textureDesc.layerCount = 1;
            textureDesc.samples = 1;
            textureDesc.usage = rhi::TextureUsage::Color_Attachment;
            mCurrentTexture = new Texture(getWgpuDevice(), textureDesc, rhi::MemoryType::Gpu_Only, handle);
        }
        else
        {
            *mCurrentTexture = handle;
        }

        rhi::TextureViewDesc viewDesc;
        viewDesc.type = rhi::TextureViewType::Texture_View_2D;
        viewDesc.format = convertRHISurfaceFormat(mSurfaceFormat);
        viewDesc.range.aspect = rhi::TextureAspect::Color;
        viewDesc.range.baseMip = 0;
        viewDesc.range.mipCount = 1;
        viewDesc.range.baseLayer = 0;
        viewDesc.range.layerCount = 1;

        mCurrentTextureView = static_cast<TextureView*>(mCurrentTexture->createView(viewDesc).get());
        return status;
    }

    rhi::TextureView* Swapchain::getCurrentTextureView() const
    {
        return mCurrentTextureView;
    }

    uint2 Swapchain::getExtent() const
    {
        return mExtent;
    }

    rhi::TextureFormat Swapchain::getSwapchainTextureFormat() const
    {
        return convertRHISurfaceFormat(mSurfaceFormat);
    }

    void Swapchain::resize(uint32_t width, uint32_t height)
    {
        mCurrentTexture = nullptr;
        mCurrentTextureView = nullptr;
        configure(width, height);
    }

    rhi::SwapchainStatus Swapchain::present()
    {
        WGPUStatus status = wgpuSurfacePresent(mSurface);
        return status == WGPUStatus_Success ? rhi::SwapchainStatus::Success : rhi::SwapchainStatus::Error;
    }
}
