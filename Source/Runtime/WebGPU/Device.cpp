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
        mShaderCompileTarget = { rhi::ShaderCodeFormat::Wgsl, 0, 0 };

        wgpuAdapterAddRef(mAdapter);

        // Query supported features.
        rhi::FeatureSupport supportedFeatures;
        FeatureSupport supportedWgpuFeatures;
        getSupportedFeatures(mAdapter, mInstance, supportedFeatures, supportedWgpuFeatures);

        // Resolve application required/optional features.
        mFeatures = rhi::resolveFeatures(supportedFeatures, desc.featureRequirements);

        // Resolve WebGPU backend required/optional features.
        FeatureRequirements wgpuRequirements;
        // WGSL immediate_address_space: 允许声明 var<immediate>, 数据由 setImmediates() 经命令编码器直接传入,
        // 大小由 GPUPipelineLayoutDescriptor.immediateSize 指定
        wgpuRequirements.immediateAddressSpace = rhi::FeaturePolicy::Required;
        // texture-formats-tier1: 新增 16 位 unorm/snorm 格式并为 r8/rg8/r16/rg16/rgb10a2/rg11b10ufloat
        // 开放 read-only/write-only 的 storage texture, 完整清单见 https://gpuweb.github.io/gpuweb/#texture-formats-tier1
        wgpuRequirements.textureFormatsTier1 = rhi::FeaturePolicy::Required;
        // texture-formats-tier2: 为 r8unorm/uint/sint、rgba8unorm/uint/sint、r16uint/sint/float、rgba16uint/sint/float、
        // rgba32uint/sint/float 开放 read-write 的 storage texture 访问
        wgpuRequirements.textureFormatsTier2 = rhi::FeaturePolicy::Required;
        // float32-filterable: 让 r32float、rg32float、rgba32float 纹理可以做 filtering 采样
        wgpuRequirements.float32Filterable = rhi::FeaturePolicy::Required;
        // primitive-index: 片元着色器可读 @builtin(primitive_index), VisBuffer 写三角形序号用
        wgpuRequirements.primitiveIndex = rhi::FeaturePolicy::Required;
        mWgpuFeatures = resolveWgpuFeatures(supportedWgpuFeatures, wgpuRequirements);

        std::vector<WGPUFeatureName> requiredFeatures = buildRequiredFeatureList(mFeatures, mWgpuFeatures);

        WGPULimits supportedLimits = {};
        wgpuAdapterGetLimits(adapter, &supportedLimits);
        WGPULimits requiredLimits = WGPU_LIMITS_INIT;
        static constexpr uint64_t kRequiredMaxBufferSize = 2ull * 1024ull * 1024ull * 1024ull;
        if (supportedLimits.maxBufferSize < kRequiredMaxBufferSize)
        {
            LOG_WARN("WebGPU supported max buffer size is less than required size");
        }
        requiredLimits.maxBufferSize = std::min(supportedLimits.maxBufferSize, kRequiredMaxBufferSize);

        static constexpr uint64_t kRequiredMaxStorageBufferBindingSize = 2ull * 1024ull * 1024ull * 1024ull;
        if (supportedLimits.maxStorageBufferBindingSize < kRequiredMaxStorageBufferBindingSize)
        {
            LOG_WARN("WebGPU supported max storage buffer binding size is less than required size");
        }
        requiredLimits.maxStorageBufferBindingSize = std::min(
            supportedLimits.maxStorageBufferBindingSize,
            kRequiredMaxStorageBufferBindingSize
        );

        // maxColorAttachmentBytesPerSample: 渲染管线输出时, 所有 color attachment 每个采样点所需字节数之和的上限, 规范默认值为 32
        static constexpr uint32_t kRequiredMaxColorAttachmentBytesPerSample = 64;
        if (supportedLimits.maxColorAttachmentBytesPerSample < kRequiredMaxColorAttachmentBytesPerSample)
        {
            LOG_WARN("WebGPU supported max color attachment bytes per sample is less than required size");
        }
        requiredLimits.maxColorAttachmentBytesPerSample = std::min(
            supportedLimits.maxColorAttachmentBytesPerSample,
            kRequiredMaxColorAttachmentBytesPerSample
        );

        // maxTextureDimension2D: dimension 为 "2d" 的纹理 size.width 和 size.height 的上限, 规范默认值为 8192
        static constexpr uint32_t kRequiredMaxTextureDimension2D = 16384;
        if (supportedLimits.maxTextureDimension2D < kRequiredMaxTextureDimension2D)
        {
            LOG_WARN("WebGPU supported max texture dimension 2D is less than required size");
        }
        requiredLimits.maxTextureDimension2D = std::min(
            supportedLimits.maxTextureDimension2D,
            kRequiredMaxTextureDimension2D
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

    TRef<rhi::Buffer> Device::createBufferImpl(const rhi::BufferDesc& desc, rhi::MemoryType memoryType) const
    {
        return new Buffer(*this, desc, memoryType);
    }

    TRef<rhi::Texture> Device::createTextureImpl(const rhi::TextureDesc& desc, rhi::MemoryType memoryType) const
    {
        return new Texture(*this, desc, memoryType);
    }

    TRef<rhi::Sampler> Device::createSamplerImpl(const rhi::SamplerDesc& desc) const
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

