#include "RenderBackend.h"

#include <Vulkan/Context.h>
#include <WebGPU/Context.h>
#include <cassert>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#endif

namespace hyp
{
    // brick 边长用 DistanceFieldBrickAtlas.h 里定义的 kDistanceFieldBrickSize, 不在这里另外定义一份。
    // 20^3 个 brick 槽位, atlas 纹理尺寸 160^3 体素, 留在常见 3D 纹理单轴上限(数百到
    // 2048 体素)以内一大截; 后续如果实际用量经常顶到这个上限, 再考虑调大或者做真正的
    // 扩容支持
    static constexpr uint32_t kDistanceFieldBrickAtlasCapacity = 8000;

    // Mesh 资产级别数据表容量, 单位是 uint32 个数——每个网格占用 kMeshShapeMetadataWordCount
    // + brickGridResolution.x*y*z(间接索引表, 逻辑体素网格总格数, 不是实际分配出去的
    // brick 数: 没有 brick 的格子也要存一个 kInvalidDistanceFieldBrickIndex 占位) +
    // kMeshShapeCardLookupWordCount + cardCount*kMeshShapeCardWordCount
    // 个条目。65536 个条目(256KB)在当前测试规模(几个网格, 每个网格 brickGridResolution
    // 在十位数左右、card 数在个位到十位数)下绰绰有余, 用量顶到上限再调大
    static constexpr uint32_t kMeshShapeDataTableCapacity = 65536;

    static std::unique_ptr<rhi::Context> createContext(rhi::Backend backend, const Window& window)
    {
        rhi::ContextDesc desc;
        desc.backend = backend;
#ifndef NDEBUG
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
        WindowingSystem windowingSystem = Window::getWindowingSystem();
        if (windowingSystem == WindowingSystem::Wayland)
        {
            desc.swapchainDesc.nativeWindow.type = rhi::NativeWindowType::Wayland;
            void* waylandDisplay = nullptr;
            window.getWaylandDisplay(&waylandDisplay);
            void* waylandSurface = nullptr;
            window.getWaylandSurface(&waylandSurface);
            desc.swapchainDesc.nativeWindow.wayland.display = waylandDisplay;
            desc.swapchainDesc.nativeWindow.wayland.surface = waylandSurface;
        }
        else if (windowingSystem == WindowingSystem::Xcb)
        {
            desc.swapchainDesc.nativeWindow.type = rhi::NativeWindowType::Xcb;
            void* connection = nullptr;
            window.getXcbConnection(&connection);
            uint32_t xcbWindow = 0;
            window.getXcbHandle(&xcbWindow);
            desc.swapchainDesc.nativeWindow.xcb.connection = connection;
            desc.swapchainDesc.nativeWindow.xcb.window = xcbWindow;
        }
        else if (windowingSystem == WindowingSystem::Xlib)
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
        else
        {
            throw std::runtime_error("Unsupported GLFW windowing system");
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
        desc.featureRequirements.depthClamp = rhi::FeaturePolicy::Required;
        desc.featureRequirements.computeShaderDerivatives = rhi::FeaturePolicy::Optional;

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

    RenderBackend::RenderBackend(const Window& window, rhi::Backend rhiBackend, const std::filesystem::path& shaderDir, const std::filesystem::path& resourceDir) :
        mWindow(window),
        mRHIContext(createContext(rhiBackend, window)),
        mCommandQueue(1024 * 1024 - RenderCommandQueue::kHeaderSize),
#ifndef HYP_SINGLE_THREADED_RENDERING
        mRenderThreadRunning(true),
#endif
        mBindGroupLayoutCache(mRHIContext->getDevice()),
        mSamplerCache(mRHIContext->getDevice()),
        mDefaultResources(*this, resourceDir),
        mPipelineCache(mRHIContext->getDevice(), mBindGroupLayoutCache),
        mProxyManager(*this),
        mBindlessRegistry(isSupportBindless(mRHIContext->getDevice()), BindlessConfig{}),
        mGlobalBindings(*this),
        mTextureClearer(*this),
        mStagingAllocator(mRHIContext->getDevice(), 16 * 1024 * 1024, 8),
        mMeshDistanceFieldBrickAtlas(*this, kDistanceFieldBrickAtlasCapacity),
        mMeshShapeDataTable(*this, kMeshShapeDataTableCapacity, "MeshShapeDataTable.Buffer"),
        mRenderTagRegistry(shaderDir),
        mMaterialShaderManager(mRHIContext->getBackend(), mRHIContext->getDevice(), mRenderTagRegistry, shaderDir),
        mFrameContext(*this)
    {
#ifndef HYP_SINGLE_THREADED_RENDERING
        mRenderThread = std::thread(&RenderBackend::renderLoop, this);
#endif
    }

    RenderBackend::~RenderBackend()
    {
        assert(mShutdown && "RenderBackend::shutdown() must be called before destruction");
        drainPendingWork();
    }

    void RenderBackend::wakeUpRenderThread() noexcept
    {
#ifndef HYP_SINGLE_THREADED_RENDERING
        mWakeUpSignal.release();
#endif
    }

    void RenderBackend::shutdown()
    {
        if (mShutdown)
            return;
        mShutdown = true;

#ifndef HYP_SINGLE_THREADED_RENDERING
        if (mRenderThread.joinable())
        {
            mRenderThreadRunning.store(false, std::memory_order_release);
            mWakeUpSignal.release();
            mRenderThread.join();
        }
#endif
        drainPendingWork();
    }

    void RenderBackend::drainPendingWork()
    {
        mCommandQueue.executeAll(mFrameContext);
        mRHIContext->getDevice().waitIdle();
        mDeferredTaskQueue.flush();
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
    }
#endif

}
