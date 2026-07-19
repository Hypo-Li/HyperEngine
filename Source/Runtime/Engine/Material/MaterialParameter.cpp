#include "MaterialParameter.h"

#include "../Context.h"

namespace hyp
{
    template <> Type* Reflection::createType<SampledTexture2D>()
    {
        auto s = new TStructure<SampledTexture2D>("SampledTexture2D");
        s->addProperty("Texture", &SampledTexture2D::texture);
        s->addProperty("Sample State Override", &SampledTexture2D::sampleStateOverride);
        return s;
    }

    template <> Type* Reflection::createType<SampledTexture2DArray>()
    {
        auto s = new TStructure<SampledTexture2DArray>("SampledTexture2DArray");
        s->addProperty("Texture", &SampledTexture2DArray::texture);
        s->addProperty("Sample State Override", &SampledTexture2DArray::sampleStateOverride);
        return s;
    }

    template <> Type* Reflection::createType<SampledTexture3D>()
    {
        auto s = new TStructure<SampledTexture3D>("SampledTexture3D");
        s->addProperty("Texture", &SampledTexture3D::texture);
        s->addProperty("Sample State Override", &SampledTexture3D::sampleStateOverride);
        return s;
    }

    template <> Type* Reflection::createType<SampledTextureCube>()
    {
        auto s = new TStructure<SampledTextureCube>("SampledTextureCube");
        s->addProperty("Texture", &SampledTextureCube::texture);
        s->addProperty("Sample State Override", &SampledTextureCube::sampleStateOverride);
        return s;
    }

    template <> Type* Reflection::createType<MaterialParameterValue>()
    {
        auto s = new TStructure<MaterialParameterValue>("MaterialParameterValue");
        s->addProperty("Value", &MaterialParameterValue::mValue);
        return s;
    }

    template <> Type* Reflection::createType<MaterialParameter>()
    {
        auto s = new TStructure<MaterialParameter>("MaterialParameter");
        s->addProperty("Name", &MaterialParameter::name);
        s->addProperty("Value", &MaterialParameter::value);
        return s;
    }

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
