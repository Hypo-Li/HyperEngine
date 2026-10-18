#pragma once
#include <RHI/Features.h>

#include "VolkInclude.h"
#include <deque>
#include <any>
#include <vector>

namespace hyp::vk
{
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
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceSubgroupProperties>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;

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
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceShaderDrawParametersFeatures>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceShaderAtomicFloatFeaturesEXT>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceAccelerationStructureFeaturesKHR>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceRayTracingPipelineFeaturesKHR>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceMeshShaderFeaturesEXT>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceFragmentShadingRateFeaturesKHR>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceDynamicRenderingFeaturesKHR>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceSynchronization2FeaturesKHR>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceTimelineSemaphoreFeatures>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceBufferDeviceAddressFeatures>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceDescriptorIndexingFeatures>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceLineRasterizationFeaturesKHR>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_KHR;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT;
            else if constexpr (std::is_same_v<T, VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR>)
                return VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR;
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

    template <typename T, T DefaultValue>
    struct TFeatures
    {
        // required
        T shaderDrawParameters = DefaultValue;
        T timelineSemaphore = DefaultValue;
        T separateDepthStencilLayouts = DefaultValue;
        T maintenance1 = DefaultValue;
        T storageBufferStorageClass = DefaultValue;
        // MASKED 材质分支的 discard(见 EditorPick.slang/ShadowDepth.slang)被 Slang 编译成
        // SPIR-V 的 OpDemoteToHelperInvocation, 没有不用这个指令的退路, 所以是必选项, 不是
        // lineRasterization 那种可以静默关掉的可选特性
        T shaderDemoteToHelperInvocation = DefaultValue;

        // optional
        T synchronization2 = DefaultValue;
        T dynamicRendering = DefaultValue;
        T lineRasterization = DefaultValue;
    };

    using FeatureRequirements = TFeatures<rhi::FeaturePolicy, rhi::FeaturePolicy::Ignored>;
    using FeatureSupport = TFeatures<bool, false>;

    void getSupportedFeatures(
        uint32_t apiVersion,
        uint32_t instanceApiVersion,
        bool physicalDeviceProperties2,
        VkPhysicalDevice physicalDevice,
        rhi::FeatureSupport& features,
        FeatureSupport& vkFeatures
    );

    std::vector<const char*> getVkDeviceExtensions(
        uint32_t apiVersion,
        bool enableSwapchain,
        const rhi::FeatureSupport& deviceFeatures,
        const FeatureSupport& vkFeatures
    );

    VkPhysicalDeviceFeatures getVkPhysicalDeviceFeatures(
        const rhi::FeatureSupport& features
    );

    FeatureSupport resolveVkFeatures(
        const FeatureSupport& supported,
        const FeatureRequirements& requirements
    );

    StructureChain buildDeviceFeatureChain(
        uint32_t apiVersion,
        const rhi::FeatureSupport& features,
        const FeatureSupport& vkFeatures
    );
}
