#include "Context.h"
#include "Device.h"
#include "Swapchain.h"

#include <Core/Common/LogSystem.h>

namespace hyp::wgpu
{
    struct RequestAdapterUserData
    {
        bool done = false;
        WGPURequestAdapterStatus status;
        WGPUAdapter adapter = nullptr;
    };

    static void onAdapterRequested(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                                   WGPUStringView /*message*/, void* userdata1, void* /*userdata2*/)
    {
        auto* data = static_cast<RequestAdapterUserData*>(userdata1);
        data->status = status;
        data->adapter = adapter;
        data->done = true;
    }

    static WGPUInstance createInstance()
    {
        WGPUInstanceFeatureName requiredFeatures[] = { WGPUInstanceFeatureName_TimedWaitAny };

        // Dawn gates WGSL language features (and native features) whose FeatureStatus is
        // UnsafeExperimental/Experimental behind the "allow_unsafe_apis" toggle
        // (see dawn::native::Toggle::AllowUnsafeAPIs, checked in InstanceBase::GatherWGSLFeatures).
        // Without it, WGPUWGSLLanguageFeatureName_ImmediateAddressSpace never shows up in
        // wgpuInstanceGetWGSLLanguageFeatures and Features.cpp will never request it.
        const char* enabledToggles[] = { "allow_unsafe_apis" };

        WGPUDawnTogglesDescriptor togglesDesc = WGPU_DAWN_TOGGLES_DESCRIPTOR_INIT;
        togglesDesc.enabledToggleCount = std::size(enabledToggles);
        togglesDesc.enabledToggles = enabledToggles;

        WGPUInstanceDescriptor desc = WGPU_INSTANCE_DESCRIPTOR_INIT;
        desc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&togglesDesc);
        desc.requiredFeatureCount = std::size(requiredFeatures);
        desc.requiredFeatures = requiredFeatures;
        desc.requiredLimits = nullptr;

        return wgpuCreateInstance(&desc);
    }

    Context::Context(const rhi::ContextDesc& desc) :
        rhi::Context(rhi::Backend::WebGPU)
    {
        mInstance = createInstance();
        if (!mInstance)
        {
            LOG_ERROR("Failed to create WebGPU instance");
            return;
        }

        WGPUSurface tempSurface = nullptr;
        if (desc.swapchainDesc.nativeWindow.type != rhi::NativeWindowType::Headless)
        {
            tempSurface = Swapchain::createSurface(mInstance, desc.swapchainDesc.nativeWindow);
        }

        WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
        options.powerPreference = WGPUPowerPreference_HighPerformance;
        options.backendType = WGPUBackendType_Vulkan;
        options.forceFallbackAdapter = WGPU_FALSE;
        options.compatibleSurface = tempSurface;

        RequestAdapterUserData userdata;
        WGPURequestAdapterCallbackInfo callbackInfo = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
        callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback = onAdapterRequested;
        callbackInfo.userdata1 = &userdata;

        WGPUFuture future = wgpuInstanceRequestAdapter(mInstance, &options, callbackInfo);

        WGPUFutureWaitInfo waitInfo = {};
        waitInfo.future = future;
        waitInfo.completed = WGPU_FALSE;
        WGPUWaitStatus waitStatus = wgpuInstanceWaitAny(mInstance, 1, &waitInfo, UINT64_MAX);

        if (tempSurface)
        {
            wgpuSurfaceRelease(tempSurface);
            tempSurface = nullptr;
        }

        if (waitStatus != WGPUWaitStatus_Success ||
            !waitInfo.completed ||
            userdata.status != WGPURequestAdapterStatus_Success ||
            !userdata.adapter)
        {
            LOG_ERROR("Failed to request WebGPU adapter");
            return;
        }

        mAdapter = userdata.adapter;

        mDevice = std::make_unique<Device>(desc, mInstance, mAdapter);
    }

    Context::~Context()
    {
        mDevice.reset();

        if (mAdapter)
        {
            wgpuAdapterRelease(mAdapter);
            mAdapter = nullptr;
        }

        if (mInstance)
        {
            wgpuInstanceRelease(mInstance);
            mInstance = nullptr;
        }
    }
}
