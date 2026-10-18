#include "Device.h"
#include "BindGroup.h"
#include "Buffer.h"
#include "DescriptorAllocator.h"
#include "Pipeline.h"
#include "Texture.h"
#include "Sampler.h"
#include "ShaderModule.h"
#include "Swapchain.h"
#include "Fence.h"
#include "CommandList.h"
#include "RenderPassCache.h"
#include "PipelineCache.h"

#include <Core/Common/LogSystem.h>
#include <Core/Common/Hash.h>

#include <algorithm>
#include <cassert>

namespace hyp::vk
{
    static std::vector<VkDeviceQueueCreateInfo> getQueues(
        VkPhysicalDevice physicalDevice,
        uint32_t& graphicsQueueFamilyIndex,
        uint32_t& computeQueueFamilyIndex,
        uint32_t& transferQueueFamilyIndex
    )
    {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());

        graphicsQueueFamilyIndex = ~0u;
        computeQueueFamilyIndex = ~0u;
        transferQueueFamilyIndex = ~0u;

        std::vector<VkDeviceQueueCreateInfo> queueInfos;

        for (uint32_t familyIndex = 0;
            familyIndex < queueFamilyProperties.size();
            ++familyIndex)
        {
            bool isValidQueue = false;
            const auto& property = queueFamilyProperties[familyIndex];
            if (property.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                if (graphicsQueueFamilyIndex == ~0u)
                {
                    graphicsQueueFamilyIndex = familyIndex;
                    isValidQueue = true;
                }
            }

            if (property.queueFlags & VK_QUEUE_COMPUTE_BIT)
            {
                // 优先选择独立的 Compute Queue, 避免与 Graphics Queue 竞争
                if (computeQueueFamilyIndex == ~0u &&
                    graphicsQueueFamilyIndex != familyIndex)
                {
                    computeQueueFamilyIndex = familyIndex;
                    isValidQueue = true;
                }
            }

            if (property.queueFlags & VK_QUEUE_TRANSFER_BIT)
            {
                // 优先选择独立的 Transfer Queue, 避免与 Graphics Queue 和 Compute Queue 竞争
                if (transferQueueFamilyIndex == ~0u &&
                    (property.queueFlags & VK_QUEUE_GRAPHICS_BIT) != VK_QUEUE_GRAPHICS_BIT &&
                    (property.queueFlags & VK_QUEUE_COMPUTE_BIT) != VK_QUEUE_COMPUTE_BIT)
                {
                    transferQueueFamilyIndex = familyIndex;
                    isValidQueue = true;
                }
            }

            if (isValidQueue)
            {
                VkDeviceQueueCreateInfo& queueInfo = queueInfos.emplace_back();
                queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueInfo.queueFamilyIndex = familyIndex;
                queueInfo.queueCount = 1;
                queueInfo.pQueuePriorities = nullptr;
            }
        }
        return queueInfos;
    }

    static VkCommandPool createCommandPool(VkDevice device, uint32_t queueFamilyIndex)
    {
        VkCommandPool commandPool;
        VkCommandPoolCreateInfo commandPoolInfo = {};
        commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolInfo.queueFamilyIndex = queueFamilyIndex;
        vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool);
        return commandPool;
    }

    static DeviceDispatch createDeviceDispatch(uint32_t apiVersion, FeatureSupport& features)
    {
        DeviceDispatch dispatch;

        if (features.dynamicRendering)
        {
            if (apiVersion >= VK_API_VERSION_1_3)
            {
                dispatch.cmdBeginRendering =
                    reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(vkCmdBeginRendering);
                dispatch.cmdEndRendering =
                    reinterpret_cast<PFN_vkCmdEndRenderingKHR>(vkCmdEndRendering);
            }
            else
            {
                dispatch.cmdBeginRendering = vkCmdBeginRenderingKHR;
                dispatch.cmdEndRendering = vkCmdEndRenderingKHR;
            }

            if (!dispatch.cmdBeginRendering || !dispatch.cmdEndRendering)
            {
                LOG_ERROR("Dynamic rendering was enabled, but its Vulkan entry points are unavailable; disabling it");
                features.dynamicRendering = false;
                dispatch.cmdBeginRendering = nullptr;
                dispatch.cmdEndRendering = nullptr;
            }
        }

        if (features.synchronization2)
        {
            dispatch.cmdPipelineBarrier2 =
                apiVersion >= VK_API_VERSION_1_3
                    ? reinterpret_cast<PFN_vkCmdPipelineBarrier2KHR>(vkCmdPipelineBarrier2)
                    : vkCmdPipelineBarrier2KHR;

            if (!dispatch.cmdPipelineBarrier2)
            {
                LOG_ERROR("Synchronization2 was enabled, but vkCmdPipelineBarrier2 is unavailable; disabling it");
                features.synchronization2 = false;
            }
        }

        if (features.timelineSemaphore)
        {
            if (apiVersion >= VK_API_VERSION_1_2)
            {
                dispatch.getSemaphoreCounterValue =
                    reinterpret_cast<PFN_vkGetSemaphoreCounterValueKHR>(vkGetSemaphoreCounterValue);
                dispatch.waitSemaphores =
                    reinterpret_cast<PFN_vkWaitSemaphoresKHR>(vkWaitSemaphores);
                dispatch.signalSemaphore =
                    reinterpret_cast<PFN_vkSignalSemaphoreKHR>(vkSignalSemaphore);
            }
            else
            {
                dispatch.getSemaphoreCounterValue = vkGetSemaphoreCounterValueKHR;
                dispatch.waitSemaphores = vkWaitSemaphoresKHR;
                dispatch.signalSemaphore = vkSignalSemaphoreKHR;
            }

            const bool hasTimelineSemaphoreEntryPoints =
                dispatch.getSemaphoreCounterValue &&
                dispatch.waitSemaphores &&
                dispatch.signalSemaphore;
            if (!hasTimelineSemaphoreEntryPoints)
            {
                LOG_ERROR("Timeline semaphore is required, but its Vulkan entry points are unavailable");
                assert(false && "Required timeline semaphore entry points are unavailable");
            }
        }

        return dispatch;
    }

    Device::Device(
        const rhi::ContextDesc& desc,
        VkInstance instance,
        VkPhysicalDevice physicalDevice,
        uint32_t instanceApiVersion,
        bool physicalDeviceProperties2
    ) : mInstance(instance),
        mPhysicalDevice(physicalDevice)
    {
        // Get device api version and build optional feature chain for vkCreateDevice.
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(mPhysicalDevice, &properties);
        mApiVersion = std::min(instanceApiVersion, properties.apiVersion);
        if (mApiVersion >= VK_API_VERSION_1_3)
            mShaderCompileTarget = { rhi::ShaderCodeFormat::Spirv, 1, 6 };
        else if (mApiVersion >= VK_API_VERSION_1_2)
            mShaderCompileTarget = { rhi::ShaderCodeFormat::Spirv, 1, 5 };
        else if (mApiVersion >= VK_API_VERSION_1_1)
            mShaderCompileTarget = { rhi::ShaderCodeFormat::Spirv, 1, 3 };
        else
            mShaderCompileTarget = { rhi::ShaderCodeFormat::Spirv, 1, 0 };

        // Query supported features.
        rhi::FeatureSupport supportedFeatures;
        FeatureSupport supportedVkFeatures;
        getSupportedFeatures(
            mApiVersion,
            instanceApiVersion,
            physicalDeviceProperties2,
            mPhysicalDevice,
            supportedFeatures,
            supportedVkFeatures
        );

        // Resolve application required/optional features.
        mFeatures = rhi::resolveFeatures(supportedFeatures, desc.featureRequirements);

        // Resolve Vulkan backend required/optional features.
        FeatureRequirements vkRequirements;
        vkRequirements.shaderDrawParameters = rhi::FeaturePolicy::Required;
        vkRequirements.timelineSemaphore = rhi::FeaturePolicy::Required;
        vkRequirements.separateDepthStencilLayouts = rhi::FeaturePolicy::Required;
        vkRequirements.maintenance1 = rhi::FeaturePolicy::Required;
        vkRequirements.storageBufferStorageClass = rhi::FeaturePolicy::Required;
#if 1
        vkRequirements.synchronization2 = rhi::FeaturePolicy::Optional;
        vkRequirements.dynamicRendering = rhi::FeaturePolicy::Optional;
#else
        // for test
        vkRequirements.synchronization2 = rhi::FeaturePolicy::Ignored;
        vkRequirements.dynamicRendering = rhi::FeaturePolicy::Ignored;
#endif
        vkRequirements.lineRasterization = rhi::FeaturePolicy::Optional;        // MASKED 材质分支的 discard 会被 Slang 编译成 SPIR-V 的 OpDemoteToHelperInvocation,
        // 没有不用这个指令的退路(见 Features.h 里这个字段的注释), 必须是 Required
        vkRequirements.shaderDemoteToHelperInvocation = rhi::FeaturePolicy::Required;
        mVkFeatures = resolveVkFeatures(supportedVkFeatures, vkRequirements);

        StructureChain featureChain = buildDeviceFeatureChain(mApiVersion, mFeatures, mVkFeatures);

        // Get Queues
        std::vector<VkDeviceQueueCreateInfo> queueInfos = getQueues(
            mPhysicalDevice,
            mGraphicsQueueInfo.familyIndex,
            mComputeQueueInfo.familyIndex,
            mTransferQueueInfo.familyIndex
        );
        float queuePriority = 1.0f;
        for (auto& queueInfo : queueInfos)
        {
            queueInfo.pQueuePriorities = &queuePriority;
        }

        // Create Device
        VkDeviceCreateInfo deviceInfo = {};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.pNext = featureChain.getHead();
        deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
        deviceInfo.pQueueCreateInfos = queueInfos.data();
        bool headless = desc.swapchainDesc.nativeWindow.type == rhi::NativeWindowType::Headless;
        auto extensions = getVkDeviceExtensions(mApiVersion, headless, mFeatures, mVkFeatures);
        deviceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        deviceInfo.ppEnabledExtensionNames = extensions.data();
        deviceInfo.pEnabledFeatures = nullptr;
        if (vkCreateDevice(mPhysicalDevice, &deviceInfo, nullptr, &mDevice) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create vulkan device");
        }
        volkLoadDevice(mDevice);
        mDispatch = createDeviceDispatch(mApiVersion, mVkFeatures);

        // Create Command Pools
        vkGetDeviceQueue(mDevice, mGraphicsQueueInfo.familyIndex, 0, &mGraphicsQueueInfo.queue);
        mGraphicsQueueInfo.queueIndex = 0;
        mGraphicsCommandPool = createCommandPool(mDevice, mGraphicsQueueInfo.familyIndex);

        if (mComputeQueueInfo.familyIndex == ~0u)
        {
            mComputeQueueInfo.familyIndex = mGraphicsQueueInfo.familyIndex;
        }
        vkGetDeviceQueue(mDevice, mComputeQueueInfo.familyIndex, 0, &mComputeQueueInfo.queue);
        mComputeQueueInfo.queueIndex = 0;
        mComputeCommandPool = createCommandPool(mDevice, mComputeQueueInfo.familyIndex);

        if (mTransferQueueInfo.familyIndex == ~0u)
        {
            mTransferQueueInfo.familyIndex = mComputeQueueInfo.familyIndex;
        }
        vkGetDeviceQueue(mDevice, mTransferQueueInfo.familyIndex, 0, &mTransferQueueInfo.queue);
        mTransferQueueInfo.queueIndex = 0;
        mTransferCommandPool = createCommandPool(mDevice, mTransferQueueInfo.familyIndex);

        // Create Allocator
        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.flags = 0;// VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        allocatorInfo.physicalDevice = mPhysicalDevice;
        allocatorInfo.device = mDevice;
        allocatorInfo.instance = mInstance;
        VmaVulkanFunctions vulkanFunctions = {};
        VkResult result = vmaImportVulkanFunctionsFromVolk(&allocatorInfo, &vulkanFunctions);
        allocatorInfo.pVulkanFunctions = &vulkanFunctions;
        vmaCreateAllocator(&allocatorInfo, &mAllocator);

        // Create Render Pass and Framebuffer caches
        mRenderPassCache = std::make_unique<RenderPassCache>(*this);
        mFramebufferCache = std::make_unique<FramebufferCache>(*this);

        // Create (and load from disk) the persistent pipeline cache
        mPipelineCache = std::make_unique<PipelineCache>(*this);

        // Create Swapchain
        if (!headless)
            mSwapchain = std::make_unique<Swapchain>(*this, desc.swapchainDesc);

        // Create Descriptor Allocator
        mDescriptorAllocator = std::make_unique<DescriptorAllocator>(*this);
    }

    Device::~Device()
    {
        mDescriptorAllocator = nullptr;
        if (mSwapchain)
            mSwapchain = nullptr;
        mPipelineCache = nullptr;
        mFramebufferCache = nullptr;
        mRenderPassCache = nullptr;
        vmaDestroyAllocator(mAllocator);
        vkDestroyCommandPool(mDevice, mTransferCommandPool, nullptr);
        vkDestroyCommandPool(mDevice, mComputeCommandPool, nullptr);
        vkDestroyCommandPool(mDevice, mGraphicsCommandPool, nullptr);
        vkDestroyDevice(mDevice, nullptr);
    }

    void Device::waitIdle() const
    {
        vkDeviceWaitIdle(mDevice);
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
        auto* vkLayout = static_cast<const BindGroupLayout*>(&layout);
        return new BindGroup(*this, *vkLayout);
    }

    TRef<rhi::ShaderModule> Device::createShaderModule(rhi::ShaderStage stage, std::vector<uint8_t>&& code, rhi::ShaderCodeFormat codeFormat) const
    {
        if (codeFormat != rhi::ShaderCodeFormat::Spirv)
        {
            LOG_ERROR("Vulkan only supports SPIR-V shader code");
            return nullptr;
        }
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
        VkCommandPool commandPool = VK_NULL_HANDLE;
        switch (queueType)
        {
        case rhi::QueueType::Graphics:
            commandPool = mGraphicsCommandPool;
            break;
        case rhi::QueueType::Compute:
            commandPool = mComputeCommandPool;
            break;
        case rhi::QueueType::Transfer:
            commandPool = mTransferCommandPool;
            break;
        }
        return new CommandList(*this, queueType, commandPool);
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
        CommandList* vkCommandList = static_cast<CommandList*>(commandList);
        vkCommandList->decode();

        VkSemaphore waitSemaphores[2];
        uint64_t waitValues[2];
        VkPipelineStageFlags waitStages[2];
        uint32_t waitSemaphoreCount = 0;
        if (waitFence)
        {
            waitSemaphores[waitSemaphoreCount] = static_cast<Fence*>(waitFence)->getHandle();
            waitValues[waitSemaphoreCount] = waitValue;
            waitStages[waitSemaphoreCount] = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            waitSemaphoreCount++;
        }
        if (commandList->getQueueType() == rhi::QueueType::Graphics &&
            enumHasAnyFlags(flags, rhi::SubmitFlags::Wait_Swapchain_Image) && mSwapchain.get())
        {
            waitSemaphores[waitSemaphoreCount] = static_cast<Swapchain*>(mSwapchain.get())->getCurrentImageAvailableSemaphore();
            waitValues[waitSemaphoreCount] = 0;
            waitStages[waitSemaphoreCount] = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            waitSemaphoreCount++;
        }

        VkSemaphore signalSemaphores[2];
        uint64_t signalValues[2];
        uint32_t signalSemaphoreCount = 0;
        if (signalFence)
        {
            signalSemaphores[signalSemaphoreCount] = static_cast<Fence*>(signalFence)->getHandle();
            signalValues[signalSemaphoreCount] = signalValue;
            signalSemaphoreCount++;
        }
        if (commandList->getQueueType() == rhi::QueueType::Graphics &&
            enumHasAnyFlags(flags, rhi::SubmitFlags::Signal_Present_Ready) && mSwapchain.get())
        {
            signalSemaphores[signalSemaphoreCount] = static_cast<Swapchain*>(mSwapchain.get())->getRenderFinishedSemaphore();
            signalValues[signalSemaphoreCount] = 0;
            signalSemaphoreCount++;
        }

        VkTimelineSemaphoreSubmitInfo timelineInfo = {};
        timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        timelineInfo.waitSemaphoreValueCount = waitSemaphoreCount;
        timelineInfo.pWaitSemaphoreValues = waitValues;
        timelineInfo.signalSemaphoreValueCount = signalSemaphoreCount;
        timelineInfo.pSignalSemaphoreValues = signalValues;

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pNext = &timelineInfo;
        submitInfo.waitSemaphoreCount = waitSemaphoreCount;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        VkCommandBuffer commandBuffers[] = { vkCommandList->getHandle() };
        submitInfo.pCommandBuffers = commandBuffers;
        submitInfo.signalSemaphoreCount = signalSemaphoreCount;
        submitInfo.pSignalSemaphores = signalSemaphores;

        VkQueue queue = VK_NULL_HANDLE;
        switch (commandList->getQueueType())
        {
        case rhi::QueueType::Graphics:
            queue = mGraphicsQueueInfo.queue;
            break;
        case rhi::QueueType::Compute:
            queue = mComputeQueueInfo.queue;
            break;
        case rhi::QueueType::Transfer:
            queue = mTransferQueueInfo.queue;
            break;
        }
        VkResult submitResult = vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        assert(submitResult == VK_SUCCESS);
    }

}
