#include "Features.h"

#include <Core/Common/LogSystem.h>
#include <algorithm>

namespace hyp::vk
{
    void getSupportedFeatures(uint32_t apiVersion, VkPhysicalDevice physicalDevice, rhi::FeatureSupport& features, FeatureSupport& vkFeatures)
    {
        auto hasExtension = [&](const char* name) -> bool
            {
                uint32_t extensionCount = 0;
                vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
                if (extensionCount == 0)
                    return false;

                std::vector<VkExtensionProperties> availableExtensions(extensionCount);
                vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, availableExtensions.data());

                return std::any_of(availableExtensions.begin(), availableExtensions.end(),
                    [&](const VkExtensionProperties& ext)
                    {
                        return std::strcmp(ext.extensionName, name) == 0;
                    });
            };

        // Vulkan 1.0 without VK_KHR_get_physical_device_properties2 cannot use vkGetPhysicalDeviceFeatures2.
        const bool hasPhysicalDeviceProperties2Ext = hasExtension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

        VkPhysicalDeviceFeatures baseFeatures = {};
        vkGetPhysicalDeviceFeatures(physicalDevice, &baseFeatures);

        StructureChain featureChain;
        auto& features2 = featureChain.add<VkPhysicalDeviceFeatures2>();
        features2.features = baseFeatures;

        VkPhysicalDeviceVulkan11Features* features11 = nullptr;
        VkPhysicalDeviceVulkan12Features* features12 = nullptr;
        VkPhysicalDeviceVulkan13Features* features13 = nullptr;
        VkPhysicalDeviceVulkan14Features* features14 = nullptr;

        VkPhysicalDeviceDynamicRenderingFeaturesKHR* dynamicRenderingFeatures = nullptr;
        VkPhysicalDeviceSynchronization2FeaturesKHR* sync2Features = nullptr;
        VkPhysicalDeviceTimelineSemaphoreFeatures* timelineFeatures = nullptr;
        VkPhysicalDeviceDescriptorIndexingFeatures* descriptorIndexingFeatures = nullptr;
        VkPhysicalDeviceShaderDrawParametersFeatures* shaderDrawParamsFeatures = nullptr;
        VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures* separateDepthStencilLayouts = nullptr;
        VkPhysicalDeviceShaderAtomicFloatFeaturesEXT* shaderAtomicFloatFeatures = nullptr;
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR* rayTracingPipelineFeatures = nullptr;
        VkPhysicalDeviceAccelerationStructureFeaturesKHR* accelerationStructureFeatures = nullptr;
        VkPhysicalDeviceFragmentShadingRateFeaturesKHR* fragmentShadingRateFeatures = nullptr;

        const bool canUseFeatures2 = (apiVersion >= VK_API_VERSION_1_1) || hasPhysicalDeviceProperties2Ext;

        if (canUseFeatures2)
        {
            if (apiVersion >= VK_API_VERSION_1_1)
                features11 = &featureChain.add<VkPhysicalDeviceVulkan11Features>();
            if (apiVersion >= VK_API_VERSION_1_2)
                features12 = &featureChain.add<VkPhysicalDeviceVulkan12Features>();
            if (apiVersion >= VK_API_VERSION_1_3)
                features13 = &featureChain.add<VkPhysicalDeviceVulkan13Features>();
            if (apiVersion >= VK_API_VERSION_1_4)
                features14 = &featureChain.add<VkPhysicalDeviceVulkan14Features>();

            if (apiVersion < VK_API_VERSION_1_3 && hasExtension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME))
                dynamicRenderingFeatures = &featureChain.add<VkPhysicalDeviceDynamicRenderingFeaturesKHR>();

            if (apiVersion < VK_API_VERSION_1_3 && hasExtension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME))
                sync2Features = &featureChain.add<VkPhysicalDeviceSynchronization2FeaturesKHR>();

            if (apiVersion < VK_API_VERSION_1_2 && hasExtension(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME))
                timelineFeatures = &featureChain.add<VkPhysicalDeviceTimelineSemaphoreFeatures>();

            if (apiVersion < VK_API_VERSION_1_2 && hasExtension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME))
                descriptorIndexingFeatures = &featureChain.add<VkPhysicalDeviceDescriptorIndexingFeatures>();

            if (apiVersion < VK_API_VERSION_1_1 && hasExtension(VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME))
                shaderDrawParamsFeatures = &featureChain.add<VkPhysicalDeviceShaderDrawParametersFeatures>();

            if (apiVersion < VK_API_VERSION_1_2 && hasExtension(VK_KHR_SEPARATE_DEPTH_STENCIL_LAYOUTS_EXTENSION_NAME))
                separateDepthStencilLayouts = &featureChain.add<VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures>();

            // 下面这几个都没有被提升进任何 core Vulkan 版本,不管 apiVersion 多高都得靠扩展查
            const bool hasShaderAtomicFloatExt = hasExtension(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);
            if (hasShaderAtomicFloatExt)
                shaderAtomicFloatFeatures = &featureChain.add<VkPhysicalDeviceShaderAtomicFloatFeaturesEXT>();

            // VK_KHR_ray_tracing_pipeline 依赖 VK_KHR_acceleration_structure,两个扩展都在才算真正可用
            const bool hasRayTracingExt =
                hasExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) &&
                hasExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            if (hasRayTracingExt)
            {
                rayTracingPipelineFeatures = &featureChain.add<VkPhysicalDeviceRayTracingPipelineFeaturesKHR>();
                accelerationStructureFeatures = &featureChain.add<VkPhysicalDeviceAccelerationStructureFeaturesKHR>();
            }

            const bool hasFragmentShadingRateExt = hasExtension(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME);
            if (hasFragmentShadingRateExt)
                fragmentShadingRateFeatures = &featureChain.add<VkPhysicalDeviceFragmentShadingRateFeaturesKHR>();

            if (apiVersion >= VK_API_VERSION_1_1)
            {
                vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);
            }
            else
            {
                auto pfn = (PFN_vkGetPhysicalDeviceFeatures2KHR)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkGetPhysicalDeviceFeatures2KHR");
                if (pfn)
                    pfn(physicalDevice, &features2);
            }

            baseFeatures = features2.features;
        }

        features.samplerAnisotropy = baseFeatures.samplerAnisotropy == VK_TRUE;
        features.independentBlend = baseFeatures.independentBlend == VK_TRUE;
        features.dualSourceBlend = baseFeatures.dualSrcBlend == VK_TRUE;
        features.multiViewport = baseFeatures.multiViewport == VK_TRUE;

        features.shaderInt16 = baseFeatures.shaderInt16 == VK_TRUE;
        features.shaderFloat16 = (features12 && features12->shaderFloat16 == VK_TRUE);
        features.shaderInt64 = baseFeatures.shaderInt64 == VK_TRUE;
        features.shaderFloat64 = baseFeatures.shaderFloat64 == VK_TRUE;
        features.shaderAtomics =
            (baseFeatures.fragmentStoresAndAtomics == VK_TRUE) ||
            (baseFeatures.vertexPipelineStoresAndAtomics == VK_TRUE);
        // 只要求最常用的那个组合:storage buffer 上的 float32 原子操作 + atomicAdd,能覆盖引擎里
        // histogram/累加类用法;shared/image、float64 那些更细分的子特性不在这个总开关的判断范围内
        features.shaderFloatAtomics =
            shaderAtomicFloatFeatures &&
            shaderAtomicFloatFeatures->shaderBufferFloat32Atomics == VK_TRUE &&
            shaderAtomicFloatFeatures->shaderBufferFloat32AtomicAdd == VK_TRUE;

        // subgroup 相关的能力是 VkPhysicalDeviceProperties2 的一部分(不是 Features2),要单独查一次;
        // 要求 compute stage 支持 subgroup 操作,并且至少有 Basic/Ballot/Arithmetic 这三类(对应
        // WaveGetLaneIndex/WaveGetLaneCount、WaveActiveBallot、WaveActiveSum/WavePrefixSum 这套引擎
        // 目前实际用到的 wave 内建函数)
        features.subgroup = false;
        if (canUseFeatures2)
        {
            StructureChain propertyChain;
            auto& properties2 = propertyChain.add<VkPhysicalDeviceProperties2>();
            auto& subgroupProperties = propertyChain.add<VkPhysicalDeviceSubgroupProperties>();

            if (apiVersion >= VK_API_VERSION_1_1)
            {
                vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);
            }
            else
            {
                auto pfn = (PFN_vkGetPhysicalDeviceProperties2KHR)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkGetPhysicalDeviceProperties2KHR");
                if (pfn)
                    pfn(physicalDevice, &properties2);
            }

            constexpr VkSubgroupFeatureFlags kRequiredSubgroupOps =
                VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT | VK_SUBGROUP_FEATURE_ARITHMETIC_BIT;
            features.subgroup =
                (subgroupProperties.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) &&
                ((subgroupProperties.supportedOperations & kRequiredSubgroupOps) == kRequiredSubgroupOps);
        }

        features.depth32FloatStencil8 = true;
        features.textureCompressionBC = baseFeatures.textureCompressionBC == VK_TRUE;
        features.textureCompressionETC2 = baseFeatures.textureCompressionETC2 == VK_TRUE;
        features.textureCompressionASTC = baseFeatures.textureCompressionASTC_LDR == VK_TRUE;

        features.indirectFirstInstance = baseFeatures.drawIndirectFirstInstance == VK_TRUE;

        features.bindless =
            (features12 && features12->descriptorIndexing == VK_TRUE) ||
            (descriptorIndexingFeatures && descriptorIndexingFeatures->runtimeDescriptorArray == VK_TRUE);
        // 一条可用的光追管线需要这两个 feature 同时打开:acceleration structure(BLAS/TLAS 的构建/
        // 使用)和 ray tracing pipeline(rgen/rchit/rmiss 那套管线),缺一个都没法真正用光追
        features.rayTracing =
            rayTracingPipelineFeatures && rayTracingPipelineFeatures->rayTracingPipeline == VK_TRUE &&
            accelerationStructureFeatures && accelerationStructureFeatures->accelerationStructure == VK_TRUE;
        // 只要求 pipelineFragmentShadingRate(整个 draw 用统一 shading rate),primitive/attachment
        // 粒度的可变着色率是更细分的能力,这个总开关不判断
        features.variableRateShading =
            fragmentShadingRateFeatures && fragmentShadingRateFeatures->pipelineFragmentShadingRate == VK_TRUE;

        vkFeatures.shaderDrawParameters =
            (features11 && features11->shaderDrawParameters == VK_TRUE) ||
            (shaderDrawParamsFeatures && shaderDrawParamsFeatures->shaderDrawParameters == VK_TRUE);

        vkFeatures.separateDepthStencilLayouts =
            (features12 && features12->separateDepthStencilLayouts == VK_TRUE) ||
            (separateDepthStencilLayouts && separateDepthStencilLayouts->separateDepthStencilLayouts == VK_TRUE);

        vkFeatures.timelineSemaphore =
            (features12 && features12->timelineSemaphore == VK_TRUE) ||
            (timelineFeatures && timelineFeatures->timelineSemaphore == VK_TRUE);

        vkFeatures.synchronization2 =
            (features13 && features13->synchronization2 == VK_TRUE) ||
            (sync2Features && sync2Features->synchronization2 == VK_TRUE);

        vkFeatures.dynamicRendering =
            (features13 && features13->dynamicRendering == VK_TRUE) ||
            (dynamicRenderingFeatures && dynamicRenderingFeatures->dynamicRendering == VK_TRUE);
    }

    std::vector<const char*> getVkDeviceExtensions(
        uint32_t apiVersion,
        bool headless,
        const rhi::FeatureSupport& features,
        const FeatureSupport& vkFeatures
    )
    {
        std::vector<const char*> extensions;

        if (!headless)
        {
            extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        }

        if (vkFeatures.dynamicRendering && apiVersion < VK_API_VERSION_1_3)
        {
            extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        }

        if (vkFeatures.synchronization2 && apiVersion < VK_API_VERSION_1_3)
        {
            extensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        }

        if (vkFeatures.timelineSemaphore && apiVersion < VK_API_VERSION_1_2)
        {
            extensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
        }

        if (features.bindless && apiVersion < VK_API_VERSION_1_2)
        {
            extensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
        }

        if (vkFeatures.shaderDrawParameters && apiVersion < VK_API_VERSION_1_1)
        {
            extensions.push_back(VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME);
        }

        if (vkFeatures.separateDepthStencilLayouts && apiVersion < VK_API_VERSION_1_2)
        {
            extensions.push_back(VK_KHR_SEPARATE_DEPTH_STENCIL_LAYOUTS_EXTENSION_NAME);
        }

        return extensions;
    }

    VkPhysicalDeviceFeatures getVkPhysicalDeviceFeatures(const rhi::FeatureSupport& features)
    {
        VkPhysicalDeviceFeatures vkFeatures = {};
        vkFeatures.samplerAnisotropy = features.samplerAnisotropy ? VK_TRUE : VK_FALSE;
        vkFeatures.shaderSampledImageArrayDynamicIndexing = features.bindless ? VK_TRUE : VK_FALSE;
        vkFeatures.shaderStorageBufferArrayDynamicIndexing = features.bindless ? VK_TRUE : VK_FALSE;
        vkFeatures.shaderStorageImageArrayDynamicIndexing = features.bindless ? VK_TRUE : VK_FALSE;
        vkFeatures.shaderInt16 = features.shaderInt16 ? VK_TRUE : VK_FALSE;
        vkFeatures.shaderInt64 = features.shaderInt64 ? VK_TRUE : VK_FALSE;
        vkFeatures.shaderFloat64 = features.shaderFloat64 ? VK_TRUE : VK_FALSE;
        vkFeatures.independentBlend = features.independentBlend ? VK_TRUE : VK_FALSE;
        vkFeatures.dualSrcBlend = features.dualSourceBlend ? VK_TRUE : VK_FALSE;
        vkFeatures.multiViewport = features.multiViewport ? VK_TRUE : VK_FALSE;
        vkFeatures.textureCompressionBC = features.textureCompressionBC ? VK_TRUE : VK_FALSE;
        vkFeatures.textureCompressionETC2 = features.textureCompressionETC2 ? VK_TRUE : VK_FALSE;
        vkFeatures.textureCompressionASTC_LDR = features.textureCompressionASTC ? VK_TRUE : VK_FALSE;
        vkFeatures.drawIndirectFirstInstance = features.indirectFirstInstance ? VK_TRUE : VK_FALSE;
        return vkFeatures;
    }

    FeatureSupport resolveVkFeatures(
        const FeatureSupport& supported,
        const FeatureRequirements& requirements
    )
    {
        auto resolve = [](const char* name, bool supported, rhi::FeaturePolicy level) -> bool
            {
                if (level == rhi::FeaturePolicy::Required && !supported)
                {
                    LOG_ERROR("Required device feature '{}' is not supported", name);
                }
                else if (level == rhi::FeaturePolicy::Optional && !supported)
                {
                    LOG_INFO("Optional device feature '{}' is not supported, disabling", name);
                }
                return ((level == rhi::FeaturePolicy::Required) || (level == rhi::FeaturePolicy::Optional)) && supported;
            };

        FeatureSupport result;
        result.shaderDrawParameters = resolve("shaderDrawParameters", supported.shaderDrawParameters, requirements.shaderDrawParameters);
        result.separateDepthStencilLayouts = resolve("separateDepthStencilLayouts", supported.separateDepthStencilLayouts, requirements.separateDepthStencilLayouts);
        result.timelineSemaphore = resolve("timelineSemaphore", supported.timelineSemaphore, requirements.timelineSemaphore);
        result.synchronization2 = resolve("synchronization2", supported.synchronization2, requirements.synchronization2);
        result.dynamicRendering = resolve("dynamicRendering", supported.dynamicRendering, requirements.dynamicRendering);
        return result;
    }

    StructureChain buildDeviceFeatureChain(
        uint32_t apiVersion,
        const rhi::FeatureSupport& features,
        const FeatureSupport& vkFeatures
    )
    {
        StructureChain featureChain;
        auto& physicalDeviceFeatures2 = featureChain.add<VkPhysicalDeviceFeatures2>();
        physicalDeviceFeatures2.features = getVkPhysicalDeviceFeatures(features);

        VkPhysicalDeviceVulkan11Features* features11 = nullptr;
        VkPhysicalDeviceVulkan12Features* features12 = nullptr;
        VkPhysicalDeviceVulkan13Features* features13 = nullptr;
        VkPhysicalDeviceVulkan14Features* features14 = nullptr;

        VkPhysicalDeviceDynamicRenderingFeaturesKHR* dynamicRenderingFeatures = nullptr;
        VkPhysicalDeviceSynchronization2FeaturesKHR* sync2Features = nullptr;
        VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures* separateDsFeatures = nullptr;
        VkPhysicalDeviceTimelineSemaphoreFeatures* timelineFeatures = nullptr;
        VkPhysicalDeviceDescriptorIndexingFeatures* descriptorIndexingFeatures = nullptr;
        VkPhysicalDeviceShaderDrawParametersFeatures* shaderDrawParametersFeatures = nullptr;
        VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures* separateDepthStencilLayoutsFeatures = nullptr;

        if (apiVersion >= VK_API_VERSION_1_1)
            features11 = &featureChain.add<VkPhysicalDeviceVulkan11Features>();
        if (apiVersion >= VK_API_VERSION_1_2)
            features12 = &featureChain.add<VkPhysicalDeviceVulkan12Features>();
        if (apiVersion >= VK_API_VERSION_1_3)
            features13 = &featureChain.add<VkPhysicalDeviceVulkan13Features>();
        if (apiVersion >= VK_API_VERSION_1_4)
            features14 = &featureChain.add<VkPhysicalDeviceVulkan14Features>();

        if (apiVersion < VK_API_VERSION_1_3 && vkFeatures.dynamicRendering)
            dynamicRenderingFeatures = &featureChain.add<VkPhysicalDeviceDynamicRenderingFeaturesKHR>();
        if (apiVersion < VK_API_VERSION_1_3 && vkFeatures.synchronization2)
            sync2Features = &featureChain.add<VkPhysicalDeviceSynchronization2FeaturesKHR>();
        if (apiVersion < VK_API_VERSION_1_2 && vkFeatures.timelineSemaphore)
            timelineFeatures = &featureChain.add<VkPhysicalDeviceTimelineSemaphoreFeatures>();
        if (apiVersion < VK_API_VERSION_1_2 && features.bindless)
            descriptorIndexingFeatures = &featureChain.add<VkPhysicalDeviceDescriptorIndexingFeatures>();
        if (apiVersion < VK_API_VERSION_1_1 && vkFeatures.shaderDrawParameters)
            shaderDrawParametersFeatures = &featureChain.add<VkPhysicalDeviceShaderDrawParametersFeatures>();
        if (apiVersion < VK_API_VERSION_1_2 && vkFeatures.separateDepthStencilLayouts)
            separateDepthStencilLayoutsFeatures = &featureChain.add<VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures>();

        if (features11)
        {
            features11->shaderDrawParameters = vkFeatures.shaderDrawParameters ? VK_TRUE : VK_FALSE;
        }
        if (shaderDrawParametersFeatures)
        {
            shaderDrawParametersFeatures->shaderDrawParameters = VK_TRUE;
        }

        if (features12)
        {
            VkBool32 bindlessEnabled = features.bindless ? VK_TRUE : VK_FALSE;
            features12->descriptorIndexing = bindlessEnabled;
            features12->shaderSampledImageArrayNonUniformIndexing = bindlessEnabled;
            features12->runtimeDescriptorArray = bindlessEnabled;
            features12->descriptorBindingPartiallyBound = bindlessEnabled;
            features12->descriptorBindingVariableDescriptorCount = bindlessEnabled;
            features12->descriptorBindingSampledImageUpdateAfterBind = bindlessEnabled;

            features12->timelineSemaphore = vkFeatures.timelineSemaphore ? VK_TRUE : VK_FALSE;

            features12->separateDepthStencilLayouts = vkFeatures.separateDepthStencilLayouts ? VK_TRUE : VK_FALSE;
        }
        if (descriptorIndexingFeatures)
        {
            descriptorIndexingFeatures->shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
            descriptorIndexingFeatures->runtimeDescriptorArray = VK_TRUE;
            descriptorIndexingFeatures->descriptorBindingPartiallyBound = VK_TRUE;
            descriptorIndexingFeatures->descriptorBindingVariableDescriptorCount = VK_TRUE;
            descriptorIndexingFeatures->descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        }
        if (timelineFeatures) timelineFeatures->timelineSemaphore = VK_TRUE;
        if (separateDepthStencilLayoutsFeatures) separateDepthStencilLayoutsFeatures->separateDepthStencilLayouts = VK_TRUE;

        if (features13)
        {
            features13->synchronization2 = vkFeatures.synchronization2 ? VK_TRUE : VK_FALSE;
            features13->dynamicRendering = vkFeatures.dynamicRendering ? VK_TRUE : VK_FALSE;
        }
        if (dynamicRenderingFeatures) dynamicRenderingFeatures->dynamicRendering = VK_TRUE;
        if (sync2Features) sync2Features->synchronization2 = VK_TRUE;

        return featureChain;
    }

}
