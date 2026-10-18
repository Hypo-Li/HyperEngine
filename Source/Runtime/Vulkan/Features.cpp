#include "Features.h"

#include <Core/Common/LogSystem.h>
#include <algorithm>
#include <cstring>

namespace hyp::vk
{
    void getSupportedFeatures(
        uint32_t apiVersion,
        uint32_t instanceApiVersion,
        bool physicalDeviceProperties2,
        VkPhysicalDevice physicalDevice,
        rhi::FeatureSupport& features,
        FeatureSupport& vkFeatures
    )
    {
        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(
            physicalDevice,
            nullptr,
            &extensionCount,
            availableExtensions.data()
        );

        auto hasExtension = [&](const char* name) -> bool
            {
                return std::any_of(availableExtensions.begin(), availableExtensions.end(),
                    [&](const VkExtensionProperties& ext)
                    {
                        return std::strcmp(ext.extensionName, name) == 0;
                    });
            };

        const bool hasFeatures2EntryPoint =
            instanceApiVersion >= VK_API_VERSION_1_1
                ? vkGetPhysicalDeviceFeatures2 != nullptr &&
                  vkGetPhysicalDeviceProperties2 != nullptr
                : vkGetPhysicalDeviceFeatures2KHR != nullptr &&
                  vkGetPhysicalDeviceProperties2KHR != nullptr;
        const bool canUseFeatures2 = physicalDeviceProperties2 && hasFeatures2EntryPoint;

        const bool hasCreateRenderPass2Dependencies =
            apiVersion >= VK_API_VERSION_1_1 ||
            (hasExtension(VK_KHR_MULTIVIEW_EXTENSION_NAME) &&
             hasExtension(VK_KHR_MAINTENANCE_2_EXTENSION_NAME));
        const bool hasCreateRenderPass2 =
            apiVersion >= VK_API_VERSION_1_2 ||
            (hasCreateRenderPass2Dependencies &&
             hasExtension(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME));
        const bool hasDepthStencilResolve =
            apiVersion >= VK_API_VERSION_1_2 ||
            (hasCreateRenderPass2 &&
             hasExtension(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME));
        const bool hasShaderDrawParametersExtension =
            apiVersion < VK_API_VERSION_1_1 &&
            hasExtension(VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME);

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
        VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures* separateDepthStencilLayouts = nullptr;
        VkPhysicalDeviceShaderAtomicFloatFeaturesEXT* shaderAtomicFloatFeatures = nullptr;
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR* rayTracingPipelineFeatures = nullptr;
        VkPhysicalDeviceAccelerationStructureFeaturesKHR* accelerationStructureFeatures = nullptr;
        VkPhysicalDeviceFragmentShadingRateFeaturesKHR* fragmentShadingRateFeatures = nullptr;
        VkPhysicalDeviceLineRasterizationFeaturesKHR* lineRasterizationFeatures = nullptr;
        VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT* shaderDemoteToHelperInvocationFeatures = nullptr;
        VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR* computeShaderDerivativesFeatures = nullptr;

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

            if (apiVersion < VK_API_VERSION_1_3 &&
                hasDepthStencilResolve &&
                hasExtension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME))
                dynamicRenderingFeatures = &featureChain.add<VkPhysicalDeviceDynamicRenderingFeaturesKHR>();

            if (apiVersion < VK_API_VERSION_1_3 &&
                hasCreateRenderPass2 &&
                hasExtension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME))
                sync2Features = &featureChain.add<VkPhysicalDeviceSynchronization2FeaturesKHR>();

            if (apiVersion < VK_API_VERSION_1_2 && hasExtension(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME))
                timelineFeatures = &featureChain.add<VkPhysicalDeviceTimelineSemaphoreFeatures>();

            if (apiVersion >= VK_API_VERSION_1_1 &&
                apiVersion < VK_API_VERSION_1_2 &&
                hasExtension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME))
                descriptorIndexingFeatures = &featureChain.add<VkPhysicalDeviceDescriptorIndexingFeatures>();

            if (apiVersion < VK_API_VERSION_1_2 &&
                hasCreateRenderPass2 &&
                hasExtension(VK_KHR_SEPARATE_DEPTH_STENCIL_LAYOUTS_EXTENSION_NAME))
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

            // Vulkan 1.4 把这个特性折进了 VkPhysicalDeviceVulkan14Features(字段名同样叫
            // smoothLines), 两个结构体不能同时出现在同一条 pNext 链上(VUID-VkDeviceCreateInfo-
            // pNext-10360), apiVersion>=1.4 时只能靠 features14 读取/设置, 不能再单独查这个扩展的
            // feature struct
            const bool hasLineRasterizationExt =
                apiVersion < VK_API_VERSION_1_4 &&
                hasExtension(VK_KHR_LINE_RASTERIZATION_EXTENSION_NAME);
            if (hasLineRasterizationExt)
                lineRasterizationFeatures = &featureChain.add<VkPhysicalDeviceLineRasterizationFeaturesKHR>();

            // Vulkan 1.3 把这个特性折进了 VkPhysicalDeviceVulkan13Features, 1.3 之前得靠
            // VK_EXT_shader_demote_to_helper_invocation 扩展的 feature struct 查
            const bool hasShaderDemoteToHelperInvocationExt =
                apiVersion < VK_API_VERSION_1_3 &&
                hasExtension(VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME);
            if (hasShaderDemoteToHelperInvocationExt)
                shaderDemoteToHelperInvocationFeatures = &featureChain.add<VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT>();

            if (hasExtension(VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME))
                computeShaderDerivativesFeatures = &featureChain.add<VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR>();

            if (instanceApiVersion >= VK_API_VERSION_1_1)
            {
                vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);
            }
            else
            {
                vkGetPhysicalDeviceFeatures2KHR(physicalDevice, &features2);
            }

            baseFeatures = features2.features;
        }

        features.samplerAnisotropy = baseFeatures.samplerAnisotropy == VK_TRUE;
        features.independentBlend = baseFeatures.independentBlend == VK_TRUE;
        features.dualSourceBlend = baseFeatures.dualSrcBlend == VK_TRUE;
        features.multiViewport = baseFeatures.multiViewport == VK_TRUE;
        features.depthClamp = baseFeatures.depthClamp == VK_TRUE;

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
        // Modern GroupNonUniform capabilities are enabled through Vulkan 1.1
        // subgroup properties. The Vulkan 1.0 EXT ballot/vote extensions are
        // not an equivalent path for the full Basic/Ballot/Arithmetic set.
        if (apiVersion >= VK_API_VERSION_1_1 && canUseFeatures2)
        {
            StructureChain propertyChain;
            auto& properties2 = propertyChain.add<VkPhysicalDeviceProperties2>();
            auto& subgroupProperties = propertyChain.add<VkPhysicalDeviceSubgroupProperties>();

            if (instanceApiVersion >= VK_API_VERSION_1_1)
            {
                vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);
            }
            else
            {
                vkGetPhysicalDeviceProperties2KHR(physicalDevice, &properties2);
            }

            constexpr VkSubgroupFeatureFlags kRequiredSubgroupOps =
                VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT | VK_SUBGROUP_FEATURE_ARITHMETIC_BIT;
            features.subgroup =
                (subgroupProperties.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) &&
                ((subgroupProperties.supportedOperations & kRequiredSubgroupOps) == kRequiredSubgroupOps);
        }

        features.computeShaderDerivatives =
            computeShaderDerivativesFeatures && computeShaderDerivativesFeatures->computeDerivativeGroupLinear == VK_TRUE;

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
            hasShaderDrawParametersExtension;

        vkFeatures.maintenance1 =
            apiVersion >= VK_API_VERSION_1_1 ||
            hasExtension(VK_KHR_MAINTENANCE_1_EXTENSION_NAME);

        vkFeatures.storageBufferStorageClass =
            apiVersion >= VK_API_VERSION_1_1 ||
            hasExtension(VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME);

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

        vkFeatures.lineRasterization =
            (features14 && features14->smoothLines == VK_TRUE) ||
            (lineRasterizationFeatures && lineRasterizationFeatures->smoothLines == VK_TRUE);

        vkFeatures.shaderDemoteToHelperInvocation =
            (features13 && features13->shaderDemoteToHelperInvocation == VK_TRUE) ||
            (shaderDemoteToHelperInvocationFeatures && shaderDemoteToHelperInvocationFeatures->shaderDemoteToHelperInvocation == VK_TRUE);
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

        const bool needsCreateRenderPass2 =
            apiVersion < VK_API_VERSION_1_2 &&
            (vkFeatures.dynamicRendering ||
             vkFeatures.synchronization2 ||
             vkFeatures.separateDepthStencilLayouts);
        if (needsCreateRenderPass2)
        {
            if (apiVersion < VK_API_VERSION_1_1)
            {
                extensions.push_back(VK_KHR_MULTIVIEW_EXTENSION_NAME);
                extensions.push_back(VK_KHR_MAINTENANCE_2_EXTENSION_NAME);
            }
            extensions.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
        }

        if (vkFeatures.dynamicRendering && apiVersion < VK_API_VERSION_1_2)
        {
            extensions.push_back(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);
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

        if (vkFeatures.maintenance1 && apiVersion < VK_API_VERSION_1_1)
        {
            extensions.push_back(VK_KHR_MAINTENANCE_1_EXTENSION_NAME);
        }

        if (vkFeatures.storageBufferStorageClass && apiVersion < VK_API_VERSION_1_1)
        {
            extensions.push_back(VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME);
        }

        if (vkFeatures.separateDepthStencilLayouts && apiVersion < VK_API_VERSION_1_2)
        {
            extensions.push_back(VK_KHR_SEPARATE_DEPTH_STENCIL_LAYOUTS_EXTENSION_NAME);
        }

        if (vkFeatures.lineRasterization && apiVersion < VK_API_VERSION_1_4)
        {
            extensions.push_back(VK_KHR_LINE_RASTERIZATION_EXTENSION_NAME);
        }

        if (vkFeatures.shaderDemoteToHelperInvocation && apiVersion < VK_API_VERSION_1_3)
        {
            extensions.push_back(VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME);
        }

        if (features.computeShaderDerivatives)
        {
            extensions.push_back(VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);
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
        vkFeatures.depthClamp = features.depthClamp ? VK_TRUE : VK_FALSE;
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
        result.maintenance1 = resolve("maintenance1", supported.maintenance1, requirements.maintenance1);
        result.storageBufferStorageClass = resolve("storageBufferStorageClass", supported.storageBufferStorageClass, requirements.storageBufferStorageClass);
        result.separateDepthStencilLayouts = resolve("separateDepthStencilLayouts", supported.separateDepthStencilLayouts, requirements.separateDepthStencilLayouts);
        result.timelineSemaphore = resolve("timelineSemaphore", supported.timelineSemaphore, requirements.timelineSemaphore);
        result.synchronization2 = resolve("synchronization2", supported.synchronization2, requirements.synchronization2);
        result.dynamicRendering = resolve("dynamicRendering", supported.dynamicRendering, requirements.dynamicRendering);
        result.lineRasterization = resolve("lineRasterization", supported.lineRasterization, requirements.lineRasterization);
        result.shaderDemoteToHelperInvocation = resolve("shaderDemoteToHelperInvocation", supported.shaderDemoteToHelperInvocation, requirements.shaderDemoteToHelperInvocation);
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
        VkPhysicalDeviceTimelineSemaphoreFeatures* timelineFeatures = nullptr;
        VkPhysicalDeviceDescriptorIndexingFeatures* descriptorIndexingFeatures = nullptr;
        VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures* separateDepthStencilLayoutsFeatures = nullptr;
        VkPhysicalDeviceLineRasterizationFeaturesKHR* lineRasterizationFeatures = nullptr;
        VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT* shaderDemoteToHelperInvocationFeatures = nullptr;
        VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR* computeShaderDerivativesFeatures = nullptr;

        if (apiVersion >= VK_API_VERSION_1_1)
            features11 =&featureChain.add<VkPhysicalDeviceVulkan11Features>();
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
        if (apiVersion < VK_API_VERSION_1_2 && vkFeatures.separateDepthStencilLayouts)
            separateDepthStencilLayoutsFeatures = &featureChain.add<VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures>();
        if (apiVersion < VK_API_VERSION_1_4 && vkFeatures.lineRasterization)
            lineRasterizationFeatures = &featureChain.add<VkPhysicalDeviceLineRasterizationFeaturesKHR>();
        if (apiVersion < VK_API_VERSION_1_3 && vkFeatures.shaderDemoteToHelperInvocation)
            shaderDemoteToHelperInvocationFeatures = &featureChain.add<VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT>();
        if (features.computeShaderDerivatives)
            computeShaderDerivativesFeatures = &featureChain.add<VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR>();

        if (features11)
        {
            features11->shaderDrawParameters = vkFeatures.shaderDrawParameters ? VK_TRUE : VK_FALSE;
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
        if (lineRasterizationFeatures) lineRasterizationFeatures->smoothLines = VK_TRUE;

        if (features13)
        {
            features13->synchronization2 = vkFeatures.synchronization2 ? VK_TRUE : VK_FALSE;
            features13->dynamicRendering = vkFeatures.dynamicRendering ? VK_TRUE : VK_FALSE;
            features13->shaderDemoteToHelperInvocation = vkFeatures.shaderDemoteToHelperInvocation ? VK_TRUE : VK_FALSE;
        }
        if (features14)
        {
            features14->smoothLines = vkFeatures.lineRasterization ? VK_TRUE : VK_FALSE;
        }
        if (dynamicRenderingFeatures) dynamicRenderingFeatures->dynamicRendering = VK_TRUE;
        if (sync2Features) sync2Features->synchronization2 = VK_TRUE;
        if (shaderDemoteToHelperInvocationFeatures) shaderDemoteToHelperInvocationFeatures->shaderDemoteToHelperInvocation = VK_TRUE;
        if (computeShaderDerivativesFeatures) computeShaderDerivativesFeatures->computeDerivativeGroupLinear = VK_TRUE;

        return featureChain;
    }

}
