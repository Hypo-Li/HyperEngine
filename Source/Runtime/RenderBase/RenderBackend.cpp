#include "RenderBackend.h"

#include <Vulkan/Context.h>
#include <WebGPU/Context.h>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#endif

namespace hyp
{
    static std::unique_ptr<rhi::Context> createContext(rhi::Backend backend, const Window& window)
    {
        rhi::ContextDesc desc;
        desc.backend = backend;
#ifdef _DEBUG
        desc.enableValidation = true;
#else
        desc.enableValidation = false;
#endif

#ifdef _WIN32
        desc.swapchainDesc.nativeWindow.type = rhi::NativeWindowType::Win32;
        HWND hWnd;
        window.getWin32Handle(&hWnd);
        HINSTANCE hInstance = GetModuleHandle(NULL);
        desc.swapchainDesc.nativeWindow.win32.hwnd = hWnd;
        desc.swapchainDesc.nativeWindow.win32.hinstance = hInstance;
#elif defined(__linux__)
        if (Window::getWindowingSystem() == WindowingSystem::Wayland)
        {
            desc.swapchainDesc.nativeWindow.type = rhi::NativeWindowType::Wayland;
            void* waylandDisplay = nullptr;
            window.getWaylandDisplay(&waylandDisplay);
            void* waylandSurface = nullptr;
            window.getWaylandSurface(&waylandSurface);
            desc.swapchainDesc.nativeWindow.wayland.display = waylandDisplay;
            desc.swapchainDesc.nativeWindow.wayland.surface = waylandSurface;
        }
        else
        {
            desc.swapchainDesc.nativeWindow.type = rhi::NativeWindowType::Xlib;
            Display* display = nullptr;
            window.getX11Display(&display);
            // ::Window 显式取全局命名空间下 X11 的 Window(XID)类型,避免和 hyp::Window 这个类同名冲突
            ::Window x11Window = 0;
            window.getX11Handle(&x11Window);
            desc.swapchainDesc.nativeWindow.xlib.display = display;
            desc.swapchainDesc.nativeWindow.xlib.window = static_cast<uint64_t>(x11Window);
        }
#endif
        uint2 size = window.getFramebufferSize();
        desc.swapchainDesc.width = size.x;
        desc.swapchainDesc.height = size.y;
        desc.swapchainDesc.vsync = false;
        desc.featureRequirements.bindless = rhi::FeaturePolicy::Optional;
        desc.featureRequirements.samplerAnisotropy = rhi::FeaturePolicy::Required;
        desc.featureRequirements.textureCompressionBC = rhi::FeaturePolicy::Required;
        desc.featureRequirements.depth32FloatStencil8 = rhi::FeaturePolicy::Required;
        desc.featureRequirements.subgroup = rhi::FeaturePolicy::Required;

        if (backend == rhi::Backend::Vulkan)
        {
            return std::make_unique<vk::Context>(desc);
        }
        else if (backend == rhi::Backend::WebGPU)
        {
            return std::make_unique<wgpu::Context>(desc);
        }

        return nullptr;
    }

    static bool isSupportBindless(const rhi::Device& device)
    {
        return device.getFeatures().bindless == true;
    }

    RenderBackend::RenderBackend(const Window& window) :
        mWindow(window),
        mRHIContext(createContext(rhi::Backend::Vulkan, window)),
        mCommandQueue(1024 * 1024),
#ifndef HYP_SINGLE_THREADED_RENDERING
        mRenderThreadRunning(true),
#endif
        mBindGroupLayoutCache(mRHIContext->getDevice()),
        mSamplerCache(mRHIContext->getDevice()),
        mDefaultResources(*this),
        mPipelineCache(mRHIContext->getDevice()),
        mProxyManager(*this),
        mBindlessRegistry(isSupportBindless(mRHIContext->getDevice()), BindlessConfig{}),
        mGlobalBindings(*this),
        mStagingAllocator(mRHIContext->getDevice(), 16 * 1024 * 1024, 8),
        mRenderTagRegistry(),
        mMaterialShaderManager(mRHIContext->getBackend(), mRHIContext->getDevice(), mRenderTagRegistry),
        mFrameContext(*this)
    {
#ifndef HYP_SINGLE_THREADED_RENDERING
        mRenderThread = std::thread(&RenderBackend::renderLoop, this);
#endif
    }

    RenderBackend::~RenderBackend()
    {
        finishRenderThread();
    }

    void RenderBackend::wakeUpRenderThread() noexcept
    {
#ifndef HYP_SINGLE_THREADED_RENDERING
        mWakeUpSignal.release();
#endif
    }

    void RenderBackend::finishRenderThread()
    {
#ifdef HYP_SINGLE_THREADED_RENDERING
        mCommandQueue.executeAll(mFrameContext);
        mRHIContext->getDevice().waitIdle();
        mDeferredTaskQueue.flush();
#else
        mRenderThreadRunning.store(false, std::memory_order_release);
        mWakeUpSignal.release();
        if (mRenderThread.joinable())
            mRenderThread.join();
#endif
    }

#ifndef HYP_SINGLE_THREADED_RENDERING
    void RenderBackend::renderLoop()
    {
        while (mRenderThreadRunning.load(std::memory_order_acquire))
        {
            mWakeUpSignal.acquire();
            if (!mRenderThreadRunning.load(std::memory_order_acquire))
                break;
            mCommandQueue.executeAll(mFrameContext);
        }
        mCommandQueue.executeAll(mFrameContext);
        mRHIContext->getDevice().waitIdle();
        mDeferredTaskQueue.flush();
    }
#endif

}
