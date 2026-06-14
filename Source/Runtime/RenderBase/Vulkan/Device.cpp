#include "Device.h"
#include "Queue.h"
#include "CommandBuffer.h"
#include "Memory.h"
#include "DescriptorSet.h"
#include "DummyRenderPassCache.h"
#include "Sampler.h"

namespace hyp::vk
{
    Device::Device(
        const PhysicalDevice& physicalDevice,
        std::span<const char*> extensions,
        const VkPhysicalDeviceFeatures& features
    ) : mPhysicalDevice(physicalDevice)
    {
        float queuePriority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        const auto& queueFamilyProperties = mPhysicalDevice.getQueueFamilyProperties();

        uint32_t graphicsQueueFamilyIndex = ~0u;
        uint32_t computeQueueFamilyIndex = ~0u;
        uint32_t transferQueueFamilyIndex = ~0u;

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
                queueInfo.pQueuePriorities = &queuePriority;
            }
        }

        StructureChain featureChain = createFeatureChain();

        VkDeviceCreateInfo deviceInfo = {};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.pNext = featureChain.getHead();
        deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
        deviceInfo.pQueueCreateInfos = queueInfos.data();
        deviceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        deviceInfo.ppEnabledExtensionNames = extensions.data();
        deviceInfo.pEnabledFeatures = &features;

        if (vkCreateDevice(mPhysicalDevice.getHandle(), &deviceInfo, nullptr, &mDevice) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create vulkan device");
            return;
        }

        volkLoadDevice(mDevice);

        mGraphicsQueue = new Queue(*this, graphicsQueueFamilyIndex);
        mGraphicsCommandPool = new CommandPool(*this, graphicsQueueFamilyIndex, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

        if (computeQueueFamilyIndex == ~0u)
            computeQueueFamilyIndex = graphicsQueueFamilyIndex;
        mComputeQueue = new Queue(*this, computeQueueFamilyIndex);
        mComputeCommandPool = new CommandPool(*this, computeQueueFamilyIndex, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

        if (transferQueueFamilyIndex == ~0u)
            transferQueueFamilyIndex = computeQueueFamilyIndex;
        mTransferQueue = new Queue(*this, transferQueueFamilyIndex);
        mTransferCommandPool = new CommandPool(*this, transferQueueFamilyIndex, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

        mMemoryAllocator = std::make_unique<MemoryAllocator>(*this, VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT);
        mDescriptorAllocator = std::make_unique<DescriptorAllocator>(*this);
        mDummyRenderPassCache = std::make_unique<DummyRenderPassCache>(*this);
        mSamplerCache = std::make_unique<SamplerCache>(*this);
    }

    Device::~Device()
    {
        mSamplerCache = nullptr;
        mDummyRenderPassCache = nullptr;
        mDescriptorAllocator = nullptr;
        mMemoryAllocator = nullptr;
        mTransferCommandPool = nullptr;
        mTransferQueue = nullptr;
        mComputeCommandPool = nullptr;
        mComputeQueue = nullptr;
        mGraphicsCommandPool = nullptr;
        mGraphicsQueue = nullptr;
        vkDestroyDevice(mDevice, nullptr);
    }

    void Device::setupPresentQueue(VkSurfaceKHR surface)
    {
        const auto supportPresent = [surface](VkPhysicalDevice physicalDevice, Queue* queue)
            {
                VkBool32 supportsPresent = VK_FALSE;
                const uint32_t familyIndex = queue->getFamilyIndex();
                vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, familyIndex, surface, &supportsPresent);
                return (supportsPresent == VK_TRUE);
            };

        if (supportPresent(mPhysicalDevice.getHandle(), mGraphicsQueue))
            mPresentQueue = mGraphicsQueue;
        else if (supportPresent(mPhysicalDevice.getHandle(), mComputeQueue))
            mPresentQueue = mComputeQueue;
        else if (supportPresent(mPhysicalDevice.getHandle(), mTransferQueue))
            mPresentQueue = mTransferQueue;
    }

    StructureChain Device::createFeatureChain()
    {
        StructureChain chain;

        auto& vk11Features = chain.add<VkPhysicalDeviceVulkan11Features>();
        auto& supportedVk11Features = mPhysicalDevice.getFeatures<VkPhysicalDeviceVulkan11Features>();
        if (supportedVk11Features.shaderDrawParameters == VK_TRUE)
            vk11Features.shaderDrawParameters = VK_TRUE;
        if (supportedVk11Features.uniformAndStorageBuffer16BitAccess == VK_TRUE)
            vk11Features.uniformAndStorageBuffer16BitAccess = VK_TRUE;

        auto& vk12Features = chain.add<VkPhysicalDeviceVulkan12Features>();
        auto& supportedVk12Features = mPhysicalDevice.getFeatures<VkPhysicalDeviceVulkan12Features>();
        if (supportedVk12Features.shaderFloat16 == VK_TRUE)
            vk12Features.shaderFloat16 = VK_TRUE;
        if (supportedVk12Features.separateDepthStencilLayouts == VK_TRUE)
            vk12Features.separateDepthStencilLayouts = VK_TRUE;
        if (supportedVk12Features.bufferDeviceAddress == VK_TRUE)
            vk12Features.bufferDeviceAddress = VK_TRUE;
        if (supportedVk12Features.descriptorIndexing == VK_TRUE)
            vk12Features.descriptorIndexing = VK_TRUE;
        if (supportedVk12Features.descriptorBindingPartiallyBound == VK_TRUE)
            vk12Features.descriptorBindingPartiallyBound = VK_TRUE;
        if (supportedVk12Features.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE)
            vk12Features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        if (supportedVk12Features.descriptorBindingUpdateUnusedWhilePending == VK_TRUE)
            vk12Features.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
        if (supportedVk12Features.timelineSemaphore == VK_TRUE)
            vk12Features.timelineSemaphore = VK_TRUE;
        if (supportedVk12Features.runtimeDescriptorArray == VK_TRUE)
            vk12Features.runtimeDescriptorArray = VK_TRUE;
        assert(mPhysicalDevice.getFeatures<VkPhysicalDeviceVulkan12Features>().scalarBlockLayout == VK_TRUE);
        vk12Features.scalarBlockLayout = VK_TRUE;

        auto& vk13Features = chain.add<VkPhysicalDeviceVulkan13Features>();
        auto& supportedVk13Features = mPhysicalDevice.getFeatures<VkPhysicalDeviceVulkan13Features>();
        if (supportedVk13Features.dynamicRendering == VK_TRUE)
            vk13Features.dynamicRendering = VK_TRUE;
        if (supportedVk13Features.synchronization2 == VK_TRUE)
            vk13Features.synchronization2 = VK_TRUE;

        auto& vk14Features = chain.add<VkPhysicalDeviceVulkan14Features>();
        auto& supportedVk14Features = mPhysicalDevice.getFeatures<VkPhysicalDeviceVulkan14Features>();
        if (supportedVk14Features.pushDescriptor == VK_TRUE)
            vk14Features.pushDescriptor = VK_TRUE;

        assert(mPhysicalDevice.getFeatures<VkPhysicalDeviceShaderAtomicFloatFeaturesEXT>().shaderBufferFloat32AtomicAdd == VK_TRUE);
        auto& atomicFloatFeatures = chain.add<VkPhysicalDeviceShaderAtomicFloatFeaturesEXT>();
        atomicFloatFeatures.shaderBufferFloat32AtomicAdd = VK_TRUE;

        if (mPhysicalDevice.getFeatures<VkPhysicalDeviceAccelerationStructureFeaturesKHR>().accelerationStructure == VK_TRUE)
        {
            auto accelerationStructureFeatures = chain.add<VkPhysicalDeviceAccelerationStructureFeaturesKHR>();
            accelerationStructureFeatures.accelerationStructure = VK_TRUE;
        }

        if (mPhysicalDevice.getFeatures<VkPhysicalDeviceRayTracingPipelineFeaturesKHR>().rayTracingPipeline == VK_TRUE)
        {
            auto& rayTracingPipelineFeatures = chain.add<VkPhysicalDeviceRayTracingPipelineFeaturesKHR>();
            rayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
        }

        if (mPhysicalDevice.getFeatures<VkPhysicalDeviceMeshShaderFeaturesEXT>().meshShader == VK_TRUE)
        {
            auto& meshShaderFeatures = chain.add<VkPhysicalDeviceMeshShaderFeaturesEXT>();
            meshShaderFeatures.meshShader = VK_TRUE;
            meshShaderFeatures.taskShader = VK_TRUE;
        }

        return chain;
    }

}
