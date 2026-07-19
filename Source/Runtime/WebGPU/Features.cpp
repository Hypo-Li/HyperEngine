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

        features.shaderInt16 = false;
        features.shaderInt64 = false;
        features.shaderFloat16 = hasFeature(supportedFeatures, WGPUFeatureName_ShaderF16);
        features.shaderFloat64 = false;
        features.shaderAtomics = true;
        features.shaderFloatAtomics = false;
        features.subgroup = hasFeature(supportedFeatures, WGPUFeatureName_Subgroups);

        features.depth32FloatStencil8 = hasFeature(supportedFeatures, WGPUFeatureName_Depth32FloatStencil8);
        features.textureCompressionBC = hasFeature(supportedFeatures, WGPUFeatureName_TextureCompressionBC);
        features.textureCompressionETC2 = hasFeature(supportedFeatures, WGPUFeatureName_TextureCompressionETC2);
        features.textureCompressionASTC = hasFeature(supportedFeatures, WGPUFeatureName_TextureCompressionASTC);

        features.indirectFirstInstance = hasFeature(supportedFeatures, WGPUFeatureName_IndirectFirstInstance);

        features.bindless = false;
        features.rayTracing = false;
        features.variableRateShading = false;

        wgpuFeatures.immediateAddressSpace = hasWGSLLanguageFeature(supportedWGSLLanguageFeatures, WGPUWGSLLanguageFeatureName_ImmediateAddressSpace);

        wgpuSupportedWGSLLanguageFeaturesFreeMembers(supportedWGSLLanguageFeatures);
        wgpuSupportedFeaturesFreeMembers(supportedFeatures);
    }

    std::vector<WGPUFeatureName> buildRequiredFeatureList(
        const rhi::FeatureSupport& features,
        const FeatureSupport& wgpuFeatures
    )
    {
        std::vector<WGPUFeatureName> requiredFeatures;

        if (features.dualSourceBlend)
            requiredFeatures.push_back(WGPUFeatureName_DualSourceBlending);
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

    std::vector<WGPUWGSLLanguageFeatureName> buildRequiredWGSLFeatures(
        const rhi::FeatureSupport& features,
        const FeatureSupport& wgpuFeatures
    )
    {
        std::vector<WGPUWGSLLanguageFeatureName> requiredWGSLFeatures;
        if (wgpuFeatures.immediateAddressSpace)
            requiredWGSLFeatures.push_back(WGPUWGSLLanguageFeatureName_ImmediateAddressSpace);
        return requiredWGSLFeatures;
    }
}
