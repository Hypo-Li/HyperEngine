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
        T textureFormatsTier1;
        T textureFormatsTier2;
        // 核心 WebGPU 规范里 r32float 这类单通道 32 位浮点格式默认只有 UnfilterableFloat
        // 采样能力, 线性过滤(sampleType Float)是这个可选 feature 才解锁的——Vulkan/D3D 上
        // R32_SFLOAT 本身就能做线性采样, 不需要单独申请, 这是纯 WebGPU 规范特有的限制。
        // DistanceFieldBrickAtlas 的 brick atlas 就是 R32_SFLOAT 格式, MeshSDFScene.slang
        // 里对它做三线性插值(Texture3D<float> + LinearSampler)必须依赖这个 feature。
        T float32Filterable;
        T primitiveIndex;
    };

    using FeatureRequirements = TFeatures<rhi::FeaturePolicy, rhi::FeaturePolicy::Ignored>;
    using FeatureSupport = TFeatures<bool, false>;

    void getSupportedFeatures(WGPUAdapter adapter, WGPUInstance instance, rhi::FeatureSupport& features, FeatureSupport& wgpuFeatures);

    FeatureSupport resolveWgpuFeatures(
        const FeatureSupport& supported,
        const FeatureRequirements& requirements
    );

    std::vector<WGPUFeatureName> buildRequiredFeatureList(
        const rhi::FeatureSupport& features,
        const FeatureSupport& wgpuFeatures
    );
}
