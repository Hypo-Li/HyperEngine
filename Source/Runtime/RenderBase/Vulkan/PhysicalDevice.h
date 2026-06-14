#pragma once
#include <deque>
#include <any>

#include "Instance.h"

namespace hyp::vk
{
    class PhysicalDevice : public Refable
    {
    public:
        PhysicalDevice(const Instance& instance, VkPhysicalDevice physicalDevice) noexcept;

        ~PhysicalDevice() noexcept;

        VkPhysicalDevice getHandle() const noexcept
        {
            return mPhysicalDevice;
        }

        const Instance& getInstance() const noexcept
        {
            return mInstance;
        }

        template <typename T = VkPhysicalDeviceProperties>
        const T& getProperties() const noexcept;

        template <>
        const VkPhysicalDeviceProperties& getProperties() const noexcept
        {
            return mProperties.properties;
        }

        template <>
        const VkPhysicalDeviceVulkan11Properties& getProperties() const noexcept
        {
            return mVulkan11Properties;
        }

        template <>
        const VkPhysicalDeviceVulkan12Properties& getProperties() const noexcept
        {
            return mVulkan12Properties;
        }

        template <>
        const VkPhysicalDeviceVulkan13Properties& getProperties() const noexcept
        {
            return mVulkan13Properties;
        }

        template <>
        const VkPhysicalDeviceVulkan14Properties& getProperties() const noexcept
        {
            return mVulkan14Properties;
        }

        template <>
        const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& getProperties() const noexcept
        {
            return mRayTracingPipelineProperties;
        }

        template <typename T = VkPhysicalDeviceFeatures>
        const T& getFeatures() const noexcept;

        template <>
        const VkPhysicalDeviceFeatures& getFeatures() const noexcept
        {
            return mFeatures.features;
        }

        template <>
        const VkPhysicalDeviceVulkan11Features& getFeatures() const noexcept
        {
            return mVulkan11Features;
        }

        template <>
        const VkPhysicalDeviceVulkan12Features& getFeatures() const noexcept
        {
            return mVulkan12Features;
        }

        template <>
        const VkPhysicalDeviceVulkan13Features& getFeatures() const noexcept
        {
            return mVulkan13Features;
        }

        template <>
        const VkPhysicalDeviceVulkan14Features& getFeatures() const noexcept
        {
            return mVulkan14Features;
        }

        template <>
        const VkPhysicalDeviceShaderAtomicFloatFeaturesEXT& getFeatures() const noexcept
        {
            return mShaderAtomicFloatFeatures;
        }

        template <>
        const VkPhysicalDeviceAccelerationStructureFeaturesKHR& getFeatures() const noexcept
        {
            return mAccelerationStructureFeatures;
        }

        template <>
        const VkPhysicalDeviceRayTracingPipelineFeaturesKHR& getFeatures() const noexcept
        {
            return mRayTracingPipelineFeatures;
        }

        template <>
        const VkPhysicalDeviceMeshShaderFeaturesEXT& getFeatures() const noexcept
        {
            return mMeshShaderFeatures;
        }

        const std::vector<VkQueueFamilyProperties>& getQueueFamilyProperties() const noexcept
        {
            return mQueueFamilyProperties;
        }

        uint32_t findQueueFamily(VkQueueFlagBits queueFlags) const noexcept;

        const VkPhysicalDeviceMemoryProperties& getMemoryProperties() const noexcept
        {
            return mMemoryProperties;
        }

        uint32_t findMemoryType(uint32_t type, VkMemoryPropertyFlags memProperties) const noexcept;

    private:
        void queryProperties();

        void queryFeatures();

    private:
        const Instance& mInstance;
        VkPhysicalDevice mPhysicalDevice{ VK_NULL_HANDLE };

        VkPhysicalDeviceProperties2 mProperties;
        VkPhysicalDeviceVulkan11Properties mVulkan11Properties;
        VkPhysicalDeviceVulkan12Properties mVulkan12Properties;
        VkPhysicalDeviceVulkan13Properties mVulkan13Properties;
        VkPhysicalDeviceVulkan14Properties mVulkan14Properties;

        VkPhysicalDeviceRayTracingPipelinePropertiesKHR mRayTracingPipelineProperties;

        VkPhysicalDeviceFeatures2 mFeatures;
        VkPhysicalDeviceVulkan11Features mVulkan11Features;
        VkPhysicalDeviceVulkan12Features mVulkan12Features;
        VkPhysicalDeviceVulkan13Features mVulkan13Features;
        VkPhysicalDeviceVulkan14Features mVulkan14Features;

        VkPhysicalDeviceShaderAtomicFloatFeaturesEXT mShaderAtomicFloatFeatures;

        VkPhysicalDeviceAccelerationStructureFeaturesKHR mAccelerationStructureFeatures;
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR mRayTracingPipelineFeatures;

        VkPhysicalDeviceMeshShaderFeaturesEXT mMeshShaderFeatures;

        std::vector<VkQueueFamilyProperties> mQueueFamilyProperties;
        VkPhysicalDeviceMemoryProperties mMemoryProperties;
    };

    class StructureChain
    {
    public:
        template <typename T>
        T& add()
        {
            static_assert(offsetof(T, sType) == 0 && offsetof(T, pNext) == offsetof(StructureHeader, pNext));
            auto& entry = mStorage.emplace_back(T{});
            T& structure = std::any_cast<T&>(entry);

            structure.sType = getVkStructureType<T>();
            structure.pNext = nullptr;

            if (mPrev)
            {
                mPrev->pNext = &structure;
            }
            if (!mHead)
            {
                mHead = &structure;
            }

            mPrev = reinterpret_cast<StructureHeader*>(&structure);
            return structure;
        }

        void* getHead() const noexcept
        {
            return mHead;
        }

    private:
        template <typename T>
        static consteval VkStructureType getVkStructureType()
        {
            if constexpr (std::is_same_v<T, VkPhysicalDeviceProperties2>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceVulkan11Properties>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceVulkan12Properties>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceVulkan13Properties>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceVulkan14Properties>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceRayTracingPipelinePropertiesKHR>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;

            if constexpr (std::is_same_v<T, VkPhysicalDeviceFeatures2>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceVulkan11Features>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceVulkan12Features>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceVulkan13Features>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceVulkan14Features>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceShaderAtomicFloatFeaturesEXT>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceAccelerationStructureFeaturesKHR>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceRayTracingPipelineFeaturesKHR>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceMeshShaderFeaturesEXT>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
        }

    private:
        struct StructureHeader
        {
            VkStructureType sType;
            void* pNext;
        };
        void* mHead = nullptr;
        StructureHeader* mPrev = nullptr;
        std::deque<std::any> mStorage;
    };
}
