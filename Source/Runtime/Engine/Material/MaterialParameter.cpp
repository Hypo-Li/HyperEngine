#include "MaterialParameter.h"

#include "../Context.h"

namespace hyp
{
    void makeMaterialProxyParameters(
        std::span<MaterialParameter> parameters,
        std::vector<MaterialProxy::Parameter>& outProxyParameters,
        std::vector<std::pair<TextureProxyHandle, SamplerProxyHandle>>& outTextureBindings
    )
    {
        outProxyParameters.reserve(parameters.size());
        outTextureBindings.reserve(parameters.size());
        for (auto& parameter : parameters)
        {
            auto& proxyParam = outProxyParameters.emplace_back();
            proxyParam.type = parameter.value.getType();
            proxyParam.name = parameter.name;

            auto& sampleStateManager = Context::get().getSampleStateManager();
            parameter.value.visit(
                [&proxyParam, &outTextureBindings, &sampleStateManager](auto&& value)
                {
                    using ValueType = std::decay_t<decltype(value)>;
                    if constexpr (is_sampled_texture_v<ValueType>)
                    {
                        TextureProxyHandle textureProxyHandle = value.texture->getOrCreateTextureProxy();
                        SampleState sampleState = value.sampleStateOverride ?
                            *value.sampleStateOverride : value.texture->getSampleState();
                        SamplerProxyHandle samplerProxyHandle = sampleStateManager.getOrCreateSamplerProxy(sampleState);
                        outTextureBindings.emplace_back(textureProxyHandle, samplerProxyHandle);
                    }
                    else
                    {
                        proxyParam.value = value;
                    }
                }
            );
        }
    }

    void resolveTextureBinding(
        MaterialProxy::Parameter& parameter,
        TextureProxyHandle textureHandle,
        SamplerProxyHandle samplerHandle,
        ProxyManager& proxyManager
    )
    {
        if (isTextureParameterType(parameter.type))
        {
            TextureProxy* textureProxy = proxyManager.getProxy(textureHandle);
            SamplerProxy* samplerProxy = proxyManager.getProxy(samplerHandle);
            parameter.value = std::pair<TextureProxy*, SamplerProxy*>(textureProxy, samplerProxy);
        }
    }

    void resolveTextureBindings(
        std::span<MaterialProxy::Parameter> parameters,
        std::span<const std::pair<TextureProxyHandle, SamplerProxyHandle>> textureBindings,
        ProxyManager& proxyManager
    )
    {
        uint32_t textureBindingIndex = 0;
        for (auto& parameter : parameters)
        {
            if (isTextureParameterType(parameter.type))
            {
                const auto& sampledTextureHandles = textureBindings[textureBindingIndex];
                textureBindingIndex++;
                TextureProxy* textureProxy = proxyManager.getProxy(sampledTextureHandles.first);
                SamplerProxy* samplerProxy = proxyManager.getProxy(sampledTextureHandles.second);
                parameter.value = std::pair<TextureProxy*, SamplerProxy*>(textureProxy, samplerProxy);
            }
        }
    }

}
