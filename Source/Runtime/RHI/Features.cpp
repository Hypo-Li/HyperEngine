#include "Features.h"
#include <Core/Common/LogSystem.h>

namespace hyp::rhi
{
    FeatureSupport resolveFeatures(
        const FeatureSupport& supported,
        const FeatureRequirements& requirements
    )
    {
        auto resolve = [](const char* name, bool supported, FeaturePolicy level) -> bool
            {
                if (level == FeaturePolicy::Required && !supported)
                {
                    LOG_ERROR("Required device feature '{}' is not supported", name);
                }
                else if (level == FeaturePolicy::Optional && !supported)
                {
                    LOG_INFO("Optional device feature '{}' is not supported, disabling", name);
                }
                return ((level == FeaturePolicy::Required) || (level == FeaturePolicy::Optional)) && supported;
            };

        FeatureSupport result;

        // Pipeline State
        result.samplerAnisotropy = resolve("samplerAnisotropy", supported.samplerAnisotropy, requirements.samplerAnisotropy);
        result.independentBlend = resolve("independentBlend", supported.independentBlend, requirements.independentBlend);
        result.dualSourceBlend = resolve("dualSourceBlend", supported.dualSourceBlend, requirements.dualSourceBlend);
        result.multiViewport = resolve("multiViewport", supported.multiViewport, requirements.multiViewport);

        // Shader
        result.shaderInt16 = resolve("shaderInt16", supported.shaderInt16, requirements.shaderInt16);
        result.shaderFloat16 = resolve("shaderFloat16", supported.shaderFloat16, requirements.shaderFloat16);
        result.shaderInt64 = resolve("shaderInt64", supported.shaderInt64, requirements.shaderInt64);
        result.shaderFloat64 = resolve("shaderFloat64", supported.shaderFloat64, requirements.shaderFloat64);
        result.shaderAtomics = resolve("shaderAtomics", supported.shaderAtomics, requirements.shaderAtomics);
        result.shaderFloatAtomics = resolve("shaderFloatAtomics", supported.shaderFloatAtomics, requirements.shaderFloatAtomics);
        result.subgroup = resolve("subgroup", supported.subgroup, requirements.subgroup);

        // Texture
        result.depth32FloatStencil8 = resolve("depth32FloatStencil8", supported.depth32FloatStencil8, requirements.depth32FloatStencil8);
        result.textureCompressionBC = resolve("textureCompressionBC", supported.textureCompressionBC, requirements.textureCompressionBC);
        result.textureCompressionETC2 = resolve("textureCompressionETC2", supported.textureCompressionETC2, requirements.textureCompressionETC2);
        result.textureCompressionASTC = resolve("textureCompressionASTC", supported.textureCompressionASTC, requirements.textureCompressionASTC);

        // Draw Call
        result.indirectFirstInstance = resolve("indirectFirstInstance", supported.indirectFirstInstance, requirements.indirectFirstInstance);

        // Modern Techniques
        result.bindless = resolve("bindless", supported.bindless, requirements.bindless);
        result.rayTracing = resolve("rayTracing", supported.rayTracing, requirements.rayTracing);
        result.variableRateShading = resolve("variableRateShading", supported.variableRateShading, requirements.variableRateShading);

        return result;
    }
}
