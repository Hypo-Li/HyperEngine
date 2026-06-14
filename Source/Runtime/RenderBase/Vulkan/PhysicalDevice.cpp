#include "PhysicalDevice.h"

namespace hyp::vk
{
    PhysicalDevice::PhysicalDevice(const Instance& instance, VkPhysicalDevice physicalDevice) noexcept
        : mInstance(instance), mPhysicalDevice(physicalDevice)
    {
        queryProperties();
        queryFeatures();

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(mPhysicalDevice, &queueFamilyCount, nullptr);
        mQueueFamilyProperties.resize(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(mPhysicalDevice, &queueFamilyCount, mQueueFamilyProperties.data());

        vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &mMemoryProperties);
    }

    PhysicalDevice::~PhysicalDevice() noexcept
    {
    }

    uint32_t PhysicalDevice::findQueueFamily(VkQueueFlagBits queueFlags) const noexcept
    {
        int i = 0;
        for (const auto& queueFamilyProperty : mQueueFamilyProperties)
        {
            if (queueFamilyProperty.queueCount > 0 && queueFamilyProperty.queueFlags & queueFlags)
            {
                return i;
            }
            ++i;
        }
        return ~0u;
    }

    uint32_t PhysicalDevice::findMemoryType(uint32_t type, VkMemoryPropertyFlags memProperties) const noexcept
    {
        for (uint32_t i = 0; i < mMemoryProperties.memoryTypeCount; ++i)
        {
            if ((type & (1 << i)) && (mMemoryProperties.memoryTypes[i].propertyFlags & memProperties) == memProperties)
                return i;
        }
        LOG_ERROR("Failed to find suitable vulkan memory type");
        return ~0u;
    }

    void PhysicalDevice::queryProperties()
    {
        StructureChain chain;
        auto& properties = chain.add<VkPhysicalDeviceProperties2>();
        auto& vk11Properties = chain.add<VkPhysicalDeviceVulkan11Properties>();
        auto& vk12Properties = chain.add<VkPhysicalDeviceVulkan12Properties>();
        auto& vk13Properties = chain.add<VkPhysicalDeviceVulkan13Properties>();
        auto& rayTracingPipelineProperties = chain.add<VkPhysicalDeviceRayTracingPipelinePropertiesKHR>();
        // Maybe is a driver bug, querying Vulkan14 properties causes crash on some devices
        /*auto& vk14Properties = chain.add<VkPhysicalDeviceVulkan14Properties>();*/

        vkGetPhysicalDeviceProperties2(
            mPhysicalDevice,
            static_cast<VkPhysicalDeviceProperties2*>(chain.getHead())
        );

        mProperties = properties;
        mVulkan11Properties = vk11Properties;
        mVulkan12Properties = vk12Properties;
        mVulkan13Properties = vk13Properties;
        mRayTracingPipelineProperties = rayTracingPipelineProperties;
    }

    void PhysicalDevice::queryFeatures()
    {
        StructureChain chain;
        auto& features = chain.add<VkPhysicalDeviceFeatures2>();
        auto& vk11Features = chain.add<VkPhysicalDeviceVulkan11Features>();
        auto& vk12Features = chain.add<VkPhysicalDeviceVulkan12Features>();
        auto& vk13Features = chain.add<VkPhysicalDeviceVulkan13Features>();
        auto& vk14Features = chain.add<VkPhysicalDeviceVulkan14Features>();
        auto& shaderAtomicFloatFeatures = chain.add<VkPhysicalDeviceShaderAtomicFloatFeaturesEXT>();
        auto& accelerationStructureFeatures = chain.add<VkPhysicalDeviceAccelerationStructureFeaturesKHR>();
        auto& rayTracingPipelineFeatures = chain.add<VkPhysicalDeviceRayTracingPipelineFeaturesKHR>();
        auto& meshShaderFeatures = chain.add<VkPhysicalDeviceMeshShaderFeaturesEXT>();

        vkGetPhysicalDeviceFeatures2(
            mPhysicalDevice,
            static_cast<VkPhysicalDeviceFeatures2*>(chain.getHead())
        );

        mFeatures = features;
        mVulkan11Features = vk11Features;
        mVulkan12Features = vk12Features;
        mVulkan13Features = vk13Features;
        mVulkan14Features = vk14Features;
        mShaderAtomicFloatFeatures = shaderAtomicFloatFeatures;
        mAccelerationStructureFeatures = accelerationStructureFeatures;
        mRayTracingPipelineFeatures = rayTracingPipelineFeatures;
        mMeshShaderFeatures = meshShaderFeatures;
    }

}
