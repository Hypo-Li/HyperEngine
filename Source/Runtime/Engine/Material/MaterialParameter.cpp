#include "MaterialParameter.h"

#include "../Config.h"
#include "../Context.h"
#include <RenderBase/RenderFrontend.h>

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

    template <> Type* Reflection::createType<SampledFont>()
    {
        auto s = new TStructure<SampledFont>("SampledFont");
        s->addProperty("Font", &SampledFont::font);
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
        s->addProperty("Group", &MaterialParameter::group);
        return s;
    }

    std::pair<TextureProxyHandle, SamplerProxyHandle> makeTextureBinding(Texture& texture, const std::optional<SampleState>& sampleStateOverride)
    {
        SampleState sampleState = sampleStateOverride ? *sampleStateOverride : texture.getSampleState();
        SamplerProxyHandle samplerProxyHandle = Context::get().getRenderFrontend().getOrCreateSamplerProxy(
            makeSamplerDesc(sampleState, Context::get().getConfig().getGlobalSamplerSettings()));
        return { texture.getOrCreateTextureProxy(), samplerProxyHandle };
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
            if (parameter.value.getType() == MaterialParameterType::Font)
            {
                const Font& font = *parameter.value.get<SampledFont>().font.get();
                Texture2DArray& pages = *font.getPages();
                const FontParameterNames names = makeFontParameterNames(parameter.name);

                auto& pagesParam = outProxyParameters.emplace_back();
                pagesParam.type = MaterialParameterType::Texture_2D_Array;
                pagesParam.name = names.pages;
                outTextureBindings.emplace_back(makeTextureBinding(pages, std::nullopt));

                auto& sdfTypeParam = outProxyParameters.emplace_back();
                sdfTypeParam.type = MaterialParameterType::Int;
                sdfTypeParam.name = names.sdfType;
                sdfTypeParam.value = static_cast<int>(font.getSdfType());

                auto& spreadUvParam = outProxyParameters.emplace_back();
                spreadUvParam.type = MaterialParameterType::Float;
                spreadUvParam.name = names.spreadUv;
                spreadUvParam.value = font.getSpreadTexels() / static_cast<float>(pages.getDescription().width);
                continue;
            }

            auto& proxyParam = outProxyParameters.emplace_back();
            proxyParam.type = parameter.value.getType();
            proxyParam.name = parameter.name;

            parameter.value.visit(
                [&proxyParam, &outTextureBindings](auto&& value)
                {
                    using ValueType = std::decay_t<decltype(value)>;
                    if constexpr (is_sampled_texture_v<ValueType>)
                    {
                        outTextureBindings.emplace_back(makeTextureBinding(*value.texture.get(), value.sampleStateOverride));
                    }
                    else if constexpr (!std::is_same_v<ValueType, SampledFont>)
                    {
                        proxyParam.value = value;
                    }
                }
            );
        }
    }

    FontParameterNames makeFontParameterNames(HString fontParameterName)
    {
        const std::string baseName(fontParameterName.toStringView());
        return { fontParameterName, HString(baseName + "SdfType"), HString(baseName + "SpreadUv") };
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
