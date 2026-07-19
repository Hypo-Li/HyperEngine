#include "Device.h"
#include "BindGroup.h"
#include "BlobCache.h"
#include "Buffer.h"
#include "CommandList.h"
#include "Features.h"
#include "Fence.h"
#include "Pipeline.h"
#include "Sampler.h"
#include "ShaderModule.h"
#include "Swapchain.h"
#include "Texture.h"

#include <Core/Common/LogSystem.h>

#include <atomic>
#include <vector>

namespace hyp::wgpu
{
    static void wgpuUncapturedErrorCallback(WGPUDevice const* device, WGPUErrorType type, WGPUStringView message, void* userdata1, void* userdata2)
    {
        switch (type)
        {
        case WGPUErrorType_Internal:
            LOG_ERROR("WebGPU Internal error: {}", std::string_view(message.data, message.length));
            break;
        case WGPUErrorType_OutOfMemory:
            LOG_ERROR("WebGPU Out of memory: {}", std::string_view(message.data, message.length));
            break;
        case WGPUErrorType_Unknown:
            LOG_ERROR("WebGPU Unknown error: {}", std::string_view(message.data, message.length));
            break;
        case WGPUErrorType_Validation:
            LOG_ERROR("WebGPU Validation error: {}", std::string_view(message.data, message.length));
            break;
        }
    }

    Device::Device(const rhi::ContextDesc& desc, WGPUInstance instance, WGPUAdapter adapter)
        : mInstance(instance), mAdapter(adapter)
    {
        wgpuAdapterAddRef(mAdapter);

        rhi::FeatureSupport supportedFeatures;
        FeatureSupport supportedWgpuFeatures;
        getSupportedFeatures(mAdapter, mInstance, supportedFeatures, supportedWgpuFeatures);

        mFeatures = rhi::resolveFeatures(supportedFeatures, desc.featureRequirements);

        std::vector<WGPUFeatureName> requiredFeatures = buildRequiredFeatureList(mFeatures, supportedWgpuFeatures);

        WGPULimits supportedLimits = {};
        wgpuAdapterGetLimits(adapter, &supportedLimits);
        WGPULimits requiredLimits = WGPU_LIMITS_INIT;
        static constexpr uint64_t kRequiredMaxBufferSize = 512 * 1024 * 1024;
        if (supportedLimits.maxBufferSize < kRequiredMaxBufferSize)
        {
            LOG_WARN("WebGPU supported max buffer size is less than required size");
        }
        requiredLimits.maxBufferSize = std::min(supportedLimits.maxBufferSize, kRequiredMaxBufferSize);

        static constexpr uint64_t kRequiredMaxStorageBufferBindingSize = 512 * 1024 * 1024;
        if (supportedLimits.maxStorageBufferBindingSize < kRequiredMaxStorageBufferBindingSize)
        {
            LOG_WARN("WebGPU supported max storage buffer binding size is less than required size");
        }
        requiredLimits.maxStorageBufferBindingSize = std::min(
            supportedLimits.maxStorageBufferBindingSize,
            kRequiredMaxStorageBufferBindingSize
        );

        WGPUDawnCacheDeviceDescriptor cacheDesc = createDawnCacheDeviceDescriptor();

        WGPUDeviceDescriptor deviceDesc = {};
        deviceDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&cacheDesc);
        deviceDesc.label = WGPUStringView("WebGPU Device", WGPU_STRLEN);
        deviceDesc.requiredFeatureCount = static_cast<uint32_t>(requiredFeatures.size());
        deviceDesc.requiredFeatures = requiredFeatures.data();
        deviceDesc.requiredLimits = &requiredLimits;
        deviceDesc.defaultQueue.nextInChain = nullptr;
        deviceDesc.defaultQueue.label = WGPUStringView("default", WGPU_STRLEN);
        if (desc.enableValidation)
            deviceDesc.uncapturedErrorCallbackInfo.callback = wgpuUncapturedErrorCallback;

        mDevice = wgpuAdapterCreateDevice(adapter, &deviceDesc);
        if (!mDevice)
        {
            LOG_ERROR("Failed to create WebGPU device");
            return;
        }

        mQueue = wgpuDeviceGetQueue(mDevice);

        if (desc.swapchainDesc.nativeWindow.type != rhi::NativeWindowType::Headless)
            mSwapchain = std::make_unique<Swapchain>(*this, desc.swapchainDesc);
    }

    Device::~Device()
    {
        if (mSwapchain)
        {
            mSwapchain = nullptr;
        }

        if (mQueue)
        {
            wgpuQueueRelease(mQueue);
            mQueue = nullptr;
        }

        if (mDevice)
        {
            wgpuDeviceRelease(mDevice);
            mDevice = nullptr;
        }

        if (mAdapter)
        {
            wgpuAdapterRelease(mAdapter);
            mAdapter = nullptr;
        }
    }

    TRef<rhi::Buffer> Device::createBuffer(const rhi::BufferDesc& desc, rhi::MemoryType memoryType) const
    {
        return new Buffer(*this, desc, memoryType);
    }

    TRef<rhi::Texture> Device::createTexture(const rhi::TextureDesc& desc, rhi::MemoryType memoryType) const
    {
        return new Texture(*this, desc, memoryType);
    }

    TRef<rhi::Sampler> Device::createSampler(const rhi::SamplerDesc& desc) const
    {
        return new Sampler(*this, desc);
    }

    TRef<rhi::BindGroupLayout> Device::createBindGroupLayout(std::span<const rhi::BindGroupLayoutEntry> entries) const
    {
        return new BindGroupLayout(*this, entries);
    }

    TRef<rhi::BindGroup> Device::createBindGroup(const rhi::BindGroupLayout& layout) const
    {
        auto* wgpuLayout = static_cast<const BindGroupLayout*>(&layout);
        return new BindGroup(*this, *wgpuLayout);
    }

    TRef<rhi::ShaderModule> Device::createShaderModule(rhi::ShaderStage stage, std::vector<uint8_t>&& code, rhi::ShaderCodeFormat codeFormat) const
    {
        return new ShaderModule(*this, stage, std::move(code), codeFormat);
    }

    TRef<rhi::PipelineLayout> Device::createPipelineLayout(const rhi::PipelineLayoutDesc& desc) const
    {
        return new PipelineLayout(*this, desc);
    }

    TRef<rhi::Pipeline> Device::createGraphicsPipeline(const rhi::GraphicsPipelineDesc& desc) const
    {
        return new Pipeline(*this, desc);
    }

    TRef<rhi::Pipeline> Device::createComputePipeline(const rhi::ComputePipelineDesc& desc) const
    {
        return new Pipeline(*this, desc);
    }

    TRef<rhi::Fence> Device::createFence() const
    {
        return new Fence(*this);
    }

    TRef<rhi::CommandList> Device::createCommandList(rhi::QueueType queueType) const
    {
        return new CommandList(*this, queueType);
    }

    void Device::waitIdle() const
    {
        WGPUQueueWorkDoneCallbackInfo callbackInfo = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
        callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
        callbackInfo.callback = [](WGPUQueueWorkDoneStatus status, WGPUStringView /*message*/, void*, void*) {};

        WGPUFuture future = wgpuQueueOnSubmittedWorkDone(mQueue, callbackInfo);

        WGPUFutureWaitInfo waitInfo = {};
        waitInfo.future = future;
        waitInfo.completed = WGPU_FALSE;

        while (!waitInfo.completed)
        {
            WGPUWaitStatus status = wgpuInstanceWaitAny(mInstance, 1, &waitInfo, UINT64_MAX);
            if (status == WGPUWaitStatus_Error)
                break;
        }
    }

    void Device::submit(
        rhi::CommandList* commandList,
        rhi::Fence* waitFence,
        uint64_t waitValue,
        rhi::Fence* signalFence,
        uint64_t signalValue,
        rhi::SubmitFlags flags
    ) const
    {
        // WebGPU queue is FIFO, so GPU-side waitFence is not needed for same-queue ordering.
        (void)waitFence;
        (void)waitValue;

        CommandList* wgpuCommandList = static_cast<CommandList*>(commandList);
        WGPUCommandBuffer commandBuffer = wgpuCommandList->decode();
        if (commandBuffer)
        {
            wgpuQueueSubmit(mQueue, 1, &commandBuffer);
            wgpuCommandBufferRelease(commandBuffer);
        }

        if (signalFence)
        {
            WGPUQueueWorkDoneCallbackInfo callbackInfo = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
            callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
            callbackInfo.callback = [](WGPUQueueWorkDoneStatus status, WGPUStringView /*message*/, void* userdata1, void* userdata2) {
                if (status == WGPUQueueWorkDoneStatus_Success)
                {
                    auto* fence = static_cast<Fence*>(userdata1);
                    uint64_t value = reinterpret_cast<uint64_t>(userdata2);
                    fence->onQueueWorkDone(value);
                }
            };
            callbackInfo.userdata1 = signalFence;
            callbackInfo.userdata2 = reinterpret_cast<void*>(signalValue);
            WGPUFuture future = wgpuQueueOnSubmittedWorkDone(mQueue, callbackInfo);
            static_cast<Fence*>(signalFence)->addPendingSignal(signalValue, future);
        }

        wgpuInstanceProcessEvents(mInstance);
    }
}

