#pragma once
#include <Core/Reflection/Reflection.h>
#include <RHI/Sampler.h>

namespace hyp
{
    enum class Filter
    {
        Point,
        Bilinear,
        Trilinear,
        Anisotropic,
        // 追加在末尾, 不要插到中间——cook 产物里枚举是按数值序列化的(Serializer::saveEnumerationCooked),
        // 插到中间会让已 cook 的资产里排在它后面的 Filter 值全部错位
        Global,
    };

    template <> Type* Reflection::createType<Filter>();

    enum class AddressMode
    {
        Repeat,
        Clamp,
        Mirror,
    };

    template <> Type* Reflection::createType<AddressMode>();

    struct SampleState
    {
        Filter filter = Filter::Global;
        AddressMode addressModeU = AddressMode::Repeat;
        AddressMode addressModeV = AddressMode::Repeat;
        AddressMode addressModeW = AddressMode::Repeat;

        // MaterialTemplateEditor 把它整份塞进纹理参数的 undo 快照里比较, 需要支持 ==。
        friend bool operator==(const SampleState&, const SampleState&) = default;
    };

    template <> Type* Reflection::createType<SampleState>();

    // 引擎级默认采样设置(从 config.json 读一次, 不支持运行时热切, 改了要重启)——
    // Filter::Global 的纹理参数、以及新建的纹理资产, 最终都落到这份设置上
    struct GlobalSamplerSettings
    {
        Filter defaultFilter = Filter::Anisotropic;
        float maxAnisotropy = 8.0f;
        float mipLodBias = -0.3f;
    };

    rhi::SamplerDesc makeSamplerDesc(const SampleState& sampleState, const GlobalSamplerSettings& globalSettings) noexcept;
}
