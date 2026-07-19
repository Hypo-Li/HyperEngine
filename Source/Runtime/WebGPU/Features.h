#pragma once
#include <RHI/Features.h>

#include <dawn/webgpu.h>

#include <vector>

namespace hyp::wgpu
{
    template <typename T, T DefaultValue>
    struct TFeatures
    {
        T immediateAddressSpace;
    };

    using FeatureRequirements = TFeatures<rhi::FeaturePolicy, rhi::FeaturePolicy::Ignored>;
    using FeatureSupport = TFeatures<bool, false>;

    void getSupportedFeatures(WGPUAdapter adapter, WGPUInstance instance, rhi::FeatureSupport& features, FeatureSupport& wgpuFeatures);

    std::vector<WGPUFeatureName> buildRequiredFeatureList(
        const rhi::FeatureSupport& features,
        const FeatureSupport& wgpuFeatures
    );

    std::vector<WGPUWGSLLanguageFeatureName> buildRequiredWGSLFeatures(
        const rhi::FeatureSupport& features,
        const FeatureSupport& wgpuFeatures
    );
}
