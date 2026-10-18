#include "Features.h"

#include <Core/Common/LogSystem.h>

#include <algorithm>
#include <vector>

namespace hyp::wgpu
{
    static bool hasFeature(const WGPUSupportedFeatures& supportedFeatures, WGPUFeatureName feature)
    {
        const WGPUFeatureName* begin = supportedFeatures.features;
        const WGPUFeatureName* end = supportedFeatures.features + supportedFeatures.featureCount;
        return std::find(begin, end, feature) != end;
    }

    static bool hasWGSLLanguageFeature(const WGPUSupportedWGSLLanguageFeatures& supportedWGSLLanguageFeatures, WGPUWGSLLanguageFeatureName feature)
    {
        const WGPUWGSLLanguageFeatureName* begin = supportedWGSLLanguageFeatures.features;
        const WGPUWGSLLanguageFeatureName* end = supportedWGSLLanguageFeatures.features + supportedWGSLLanguageFeatures.featureCount;
        return std::find(begin, end, feature) != end;
    }

    void getSupportedFeatures(WGPUAdapter adapter, WGPUInstance instance, rhi::FeatureSupport& features, FeatureSupport& wgpuFeatures)
    {
        features = {};
        wgpuFeatures = {};

        WGPUSupportedFeatures supportedFeatures = WGPU_SUPPORTED_FEATURES_INIT;
        wgpuAdapterGetFeatures(adapter, &supportedFeatures);

        WGPUSupportedWGSLLanguageFeatures supportedWGSLLanguageFeatures = WGPU_SUPPORTED_WGSL_LANGUAGE_FEATURES_INIT;
        wgpuInstanceGetWGSLLanguageFeatures(instance, &supportedWGSLLanguageFeatures);

        features.samplerAnisotropy = true;
        features.independentBlend = true;
        features.dualSourceBlend = hasFeature(supportedFeatures, WGPUFeatureName_DualSourceBlending);
        features.multiViewport = false;
        features.depthClamp = hasFeature(supportedFeatures, WGPUFeatureName_DepthClipControl);

        features.shaderInt16 = false;
        features.shaderInt64 = false;
        features.shaderFloat16 = hasFeature(supportedFeatures, WGPUFeatureName_ShaderF16);
        features.shaderFloat64 = false;
        features.shaderAtomics = true;
        features.shaderFloatAtomics = false;
        features.subgroup = hasFeature(supportedFeatures, WGPUFeatureName_Subgroups);
        features.computeShaderDerivatives = false;

        features.depth32FloatStencil8 = hasFeature(supportedFeatures, WGPUFeatureName_Depth32FloatStencil8);
        features.textureCompressionBC = hasFeature(supportedFeatures, WGPUFeatureName_TextureCompressionBC);
        features.textureCompressionETC2 = hasFeature(supportedFeatures, WGPUFeatureName_TextureCompressionETC2);
        features.textureCompressionASTC = hasFeature(supportedFeatures, WGPUFeatureName_TextureCompressionASTC);

        features.indirectFirstInstance = hasFeature(supportedFeatures, WGPUFeatureName_IndirectFirstInstance);

        features.bindless = false;
        features.rayTracing = false;
        features.variableRateShading = false;

        wgpuFeatures.immediateAddressSpace = hasWGSLLanguageFeature(supportedWGSLLanguageFeatures, WGPUWGSLLanguageFeatureName_ImmediateAddressSpace);
        wgpuFeatures.textureFormatsTier1 =
            hasFeature(supportedFeatures, WGPUFeatureName_TextureFormatsTier1) &&
            hasWGSLLanguageFeature(supportedWGSLLanguageFeatures, WGPUWGSLLanguageFeatureName_TextureFormatsTier1);
        wgpuFeatures.textureFormatsTier2 = hasFeature(supportedFeatures, WGPUFeatureName_TextureFormatsTier2);
        wgpuFeatures.float32Filterable = hasFeature(supportedFeatures, WGPUFeatureName_Float32Filterable);
        wgpuFeatures.primitiveIndex = hasFeature(supportedFeatures, WGPUFeatureName_PrimitiveIndex);

        wgpuSupportedWGSLLanguageFeaturesFreeMembers(supportedWGSLLanguageFeatures);
        wgpuSupportedFeaturesFreeMembers(supportedFeatures);
    }

    FeatureSupport resolveWgpuFeatures(
        const FeatureSupport& supported,
        const FeatureRequirements& requirements
    )
    {
        auto resolve = [](const char* name, bool supported, rhi::FeaturePolicy level) -> bool
            {
                if (level == rhi::FeaturePolicy::Required && !supported)
                {
                    LOG_ERROR("Required WebGPU feature '{}' is not supported", name);
                }
                else if (level == rhi::FeaturePolicy::Optional && !supported)
                {
                    LOG_INFO("Optional WebGPU feature '{}' is not supported, disabling", name);
                }
                return ((level == rhi::FeaturePolicy::Required) || (level == rhi::FeaturePolicy::Optional)) && supported;
            };

        FeatureSupport result;
        result.immediateAddressSpace = resolve("immediateAddressSpace", supported.immediateAddressSpace, requirements.immediateAddressSpace);
        result.textureFormatsTier1 = resolve("textureFormatsTier1", supported.textureFormatsTier1, requirements.textureFormatsTier1);
        result.textureFormatsTier2 = resolve("textureFormatsTier2", supported.textureFormatsTier2, requirements.textureFormatsTier2);
        result.float32Filterable = resolve("float32Filterable", supported.float32Filterable, requirements.float32Filterable);
        result.primitiveIndex = resolve("primitiveIndex", supported.primitiveIndex, requirements.primitiveIndex);
        return result;
    }

    std::vector<WGPUFeatureName> buildRequiredFeatureList(
        const rhi::FeatureSupport& features,
        const FeatureSupport& wgpuFeatures
    )
    {
        std::vector<WGPUFeatureName> requiredFeatures;

        // wgpuFeatures 这里预期已经是 resolveWgpuFeatures() 处理过的结果——Required 但不支持的
        // 特性已经在那边报过 LOG_ERROR 了, 这里只管按结果决定要不要请求, 不重复判断/报错。
        if (wgpuFeatures.textureFormatsTier1)
            requiredFeatures.push_back(WGPUFeatureName_TextureFormatsTier1);
        if (wgpuFeatures.textureFormatsTier2)
            requiredFeatures.push_back(WGPUFeatureName_TextureFormatsTier2);
        if (wgpuFeatures.float32Filterable)
            requiredFeatures.push_back(WGPUFeatureName_Float32Filterable);
        if (wgpuFeatures.primitiveIndex)
            requiredFeatures.push_back(WGPUFeatureName_PrimitiveIndex);

        if (features.dualSourceBlend)
            requiredFeatures.push_back(WGPUFeatureName_DualSourceBlending);
        if (features.depthClamp)
            requiredFeatures.push_back(WGPUFeatureName_DepthClipControl);
        if (features.shaderFloat16)
            requiredFeatures.push_back(WGPUFeatureName_ShaderF16);
        if (features.subgroup)
            requiredFeatures.push_back(WGPUFeatureName_Subgroups);

        if (features.depth32FloatStencil8)
            requiredFeatures.push_back(WGPUFeatureName_Depth32FloatStencil8);
        if (features.textureCompressionBC)
            requiredFeatures.push_back(WGPUFeatureName_TextureCompressionBC);
        if (features.textureCompressionETC2)
            requiredFeatures.push_back(WGPUFeatureName_TextureCompressionETC2);
        if (features.textureCompressionASTC)
            requiredFeatures.push_back(WGPUFeatureName_TextureCompressionASTC);

        if (features.indirectFirstInstance)
            requiredFeatures.push_back(WGPUFeatureName_IndirectFirstInstance);

        return requiredFeatures;
    }
}
