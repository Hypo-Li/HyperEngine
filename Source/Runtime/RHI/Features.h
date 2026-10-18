#pragma once
#include <cstdint>

namespace hyp::rhi
{
    enum class FeaturePolicy : uint8_t
    {
        Ignored,
        Optional,
        Required,
    };

    template <typename T, T DefaultValue>
    struct TFeatures
    {
        // Pipeline State
        T samplerAnisotropy = DefaultValue;
        T independentBlend = DefaultValue;
        T dualSourceBlend = DefaultValue;
        T multiViewport = DefaultValue;
        T depthClamp = DefaultValue;

        // Shader
        T shaderInt16 = DefaultValue;
        T shaderFloat16 = DefaultValue;
        T shaderInt64 = DefaultValue;
        T shaderFloat64 = DefaultValue;
        T shaderAtomics = DefaultValue;
        T shaderFloatAtomics = DefaultValue;
        T subgroup = DefaultValue;
        T computeShaderDerivatives = DefaultValue;

        // Texture
        T depth32FloatStencil8 = DefaultValue;
        T textureCompressionBC = DefaultValue;
        T textureCompressionETC2 = DefaultValue;
        T textureCompressionASTC = DefaultValue;

        // Draw Call
        T indirectFirstInstance = DefaultValue;

        // Modern Techniques
        T bindless = DefaultValue;
        T rayTracing = DefaultValue;
        T variableRateShading = DefaultValue;
    };

    using FeatureRequirements = TFeatures<FeaturePolicy, FeaturePolicy::Ignored>;
    using FeatureSupport = TFeatures<bool, false>;

    FeatureSupport resolveFeatures(
        const FeatureSupport& supported,
        const FeatureRequirements& requirements
    );
}
