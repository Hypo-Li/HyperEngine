#pragma once
#include <unordered_map>

#include <Core/Reflection/Reflection.h>
#include <RenderBase/ProxyHandle.h>

namespace hyp
{
    enum class Filter
    {
        Point,
        Bilinear,
        Trilinear,
        Anisotropic,
    };

    enum class AddressMode
    {
        Repeat,
        Clamp,
        Mirror,
    };

    struct SampleState
    {
        Filter filter = Filter::Bilinear;
        AddressMode addressModeU = AddressMode::Repeat;
        AddressMode addressModeV = AddressMode::Repeat;
        AddressMode addressModeW = AddressMode::Repeat;

        bool operator==(const SampleState& other) const noexcept;
    };

    struct SampleStateHash
    {
        size_t operator()(const SampleState& key) const noexcept;
    };

    class SampleStateManager
    {
    public:
        SampleStateManager() = default;

        ~SampleStateManager();

        SamplerProxyHandle getOrCreateSamplerProxy(const SampleState& sampleState);

    private:
        SamplerProxyHandle createSamplerProxy(const SampleState& sampleState);

    private:
        std::unordered_map<SampleState, SamplerProxyHandle, SampleStateHash> mSamplerProxyHandleMap;
    };

    template <> Type* Reflection::createType<Filter>();

    template <> Type* Reflection::createType<AddressMode>();

    template <> Type* Reflection::createType<SampleState>();
}
