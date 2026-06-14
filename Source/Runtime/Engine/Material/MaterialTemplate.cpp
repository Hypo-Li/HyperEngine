#include "MaterialTemplate.h"
#include "MaterialInstance.h"

#include <format>

#include "../Context.h"
#include <RenderBase/RenderSystem.h>
#include <RenderBase/MaterialProxy.h>

namespace hyp
{
    REGISTER_CLASS(MaterialTemplate)

    MaterialTemplate::MaterialTemplate() :
        mDoubleSided(false),
        mTangentSpaceNormal(true)
    {
    }

    MaterialTemplate::~MaterialTemplate()
    {
        unrefMaterialShader();
    }

    void MaterialTemplate::setMaterialDomain(MaterialDomain materialDomain) noexcept
    {
        mMaterialDomain = materialDomain;
        mDirtyFlags |= DirtyFlags::Pipeline;
        mShaderHandleDirty = true;
    }

    void MaterialTemplate::setBlendMode(BlendMode blendMode) noexcept
    {
        mBlendMode = blendMode;
        mDirtyFlags |= DirtyFlags::Pipeline;
    }

    void MaterialTemplate::setAlphaCutoff(float alphaCutoff) noexcept
    {
        mAlphaCutoff = alphaCutoff;
        mDirtyFlags |= DirtyFlags::Parameters;
    }

    void MaterialTemplate::setShadingModel(ShadingModel shadingModel) noexcept
    {
        mShadingModel = shadingModel;
        mDirtyFlags |= DirtyFlags::Pipeline;
    }

    bool MaterialTemplate::addParameter(HString name, const MaterialParameterValue& defaultValue)
    {
        if (getParameterIndex(name) < mParameters.size())
            return false;

        MaterialParameter& parameter = mParameters.emplace_back();
        parameter.name = std::move(name);
        parameter.value = defaultValue;

        mDirtyFlags |= DirtyFlags::Pipeline;
        mShaderHandleDirty = true;
        return true;
    }

    HString MaterialTemplate::getParameterName(uint32_t index) const noexcept
    {
        if (index < mParameters.size())
            return mParameters[index].name;
        return HString();
    }

    uint32_t MaterialTemplate::getParameterIndex(HString name) const noexcept
    {
        auto it = std::find_if(mParameters.begin(), mParameters.end(), [name](const MaterialParameter& parameter)
        {
            return parameter.name == name;
        });
        if (it != mParameters.end())
            return static_cast<uint32_t>(std::distance(mParameters.begin(), it));
        return static_cast<uint32_t>(mParameters.size());
    }

    bool MaterialTemplate::setParameterDefaultValue(uint32_t index, const MaterialParameterValue& newDefaultValue)
    {
        assert(index < mParameters.size());
        MaterialParameter& parameter = mParameters[index];
        if (parameter.value.getType() != newDefaultValue.getType())
            return false;
        parameter.value = newDefaultValue;

        mDirtyFlags |= DirtyFlags::Parameters;

        return true;
    }

    bool MaterialTemplate::setParameterDefaultValue(HString name, const MaterialParameterValue& newDefaultValue)
    {
        return setParameterDefaultValue(getParameterIndex(name), newDefaultValue);
    }

    std::optional<MaterialParameterValue> MaterialTemplate::getParameterDefaultValue(uint32_t index) const noexcept
    {
        if (index < mParameters.size())
            return mParameters[index].value;
        return std::nullopt;
    }

    std::optional<MaterialParameterValue> MaterialTemplate::getParameterDefaultValue(HString name) const noexcept
    {
        return getParameterDefaultValue(getParameterIndex(name));
    }

    void MaterialTemplate::removeParameter(uint32_t index)
    {
        if (index >= mParameters.size())
            return;

        mParameters.erase(mParameters.begin() + index);
        mDirtyFlags |= DirtyFlags::Pipeline;
        mShaderHandleDirty = true;
    }

    void MaterialTemplate::removeParameter(HString name)
    {
        removeParameter(getParameterIndex(name));
    }

    void MaterialTemplate::setMaterialSource(std::string materialSource) noexcept
    {
        mMaterialSource = std::move(materialSource);
        mDirtyFlags |= DirtyFlags::Pipeline;
        mShaderHandleDirty = true;
    }

    void MaterialTemplate::addShaderMacro(HString name, HString value)
    {
        mShaderMacros.emplace_back(name, value);
        mDirtyFlags |= DirtyFlags::Pipeline;
    }

    void MaterialTemplate::removeShaderMacro(HString name)
    {
        mShaderMacros.erase(std::remove_if(mShaderMacros.begin(), mShaderMacros.end(), [name](const ShaderMacro& macro)
        {
            return macro.name == name;
        }), mShaderMacros.end());
        mDirtyFlags |= DirtyFlags::Pipeline;
    }

    void MaterialTemplate::setDoubleSided(bool doubleSided) noexcept
    {
        mDoubleSided = doubleSided;
        mDirtyFlags |= DirtyFlags::Pipeline;
    }

    void MaterialTemplate::setTangentSpaceNormal(bool tangentSpaceNormal) noexcept
    {
        mTangentSpaceNormal = tangentSpaceNormal;
        mDirtyFlags |= DirtyFlags::Pipeline;
    }

    void MaterialTemplate::apply()
    {
        if (mDirtyFlags == DirtyFlags::None)
            return;
        
        auto proxyHandle = getMaterialProxyHandle();
        if (proxyHandle.isValid())
        {
            if (enumHasAllFlags(mDirtyFlags, DirtyFlags::Pipeline))
            {
                unrefMaterialShader();
                updateMaterialProxy();
            }
            else if (enumHasAllFlags(mDirtyFlags, DirtyFlags::Parameters))
            {
                updateAllParameters();
            }
        }

        for (MaterialInstance* instance : mInstances)
        {
            instance->onTemplateUpdated(mDirtyFlags);
        }

        mDirtyFlags = DirtyFlags::None;
    }

    MaterialShaderHandle MaterialTemplate::getOrCreateMaterialShader()
    {
        auto& renderFrontend = Context::get().getRenderFrontend();
        if (!mShaderHandle.isValid())
        {
            auto& materialShaderManager = renderFrontend.getMaterialShaderManager();
            mShaderHandle = materialShaderManager.allocateHandle();
            mShaderHandleDirty = false;
            renderFrontend.enqueueLambda(
                [materialSource = makeParameterDefines() + mMaterialSource,
                shaderHandle = mShaderHandle
                ](RenderFrameContext& context)
                {
                    auto& materialShaderManager = context.getMaterialShaderManager();
                    materialShaderManager.createMaterialShader(
                        shaderHandle,
                        "MaterialShader",
                        materialSource
                    );
                }
            );
        }
        return mShaderHandle;
    }

    void MaterialTemplate::registerInstance(MaterialInstance* instance)
    {
        mInstances.push_back(instance);
    }

    void MaterialTemplate::unregisterInstance(MaterialInstance* instance)
    {
        mInstances.erase(std::remove(mInstances.begin(), mInstances.end(), instance), mInstances.end());
    }

    void MaterialTemplate::createMaterialProxy(MaterialProxyHandle handle)
    {
        MaterialProxy::Attributes attributes;
        attributes.domain = mMaterialDomain;
        attributes.shadingModel = mShadingModel;
        attributes.blendMode = mBlendMode;
        attributes.doubleSided = mDoubleSided;

        MaterialShaderHandle materialShaderHandle = getOrCreateMaterialShader();

        std::vector<MaterialProxy::Parameter> proxyParams;
        std::vector<std::pair<TextureProxyHandle, SamplerProxyHandle>> textureBindings;
        makeMaterialProxyParameters(mParameters, proxyParams, textureBindings);

        Context::get().getRenderFrontend().enqueueLambda(
            [handle,
            attributes,
            materialShaderHandle,
            shaderMacros = mShaderMacros,
            parameters = std::move(proxyParams),
            textureBindings = std::move(textureBindings)](RenderFrameContext& context) mutable
            {
                auto& proxyManager = context.getProxyManager();
                resolveTextureBindings(parameters, textureBindings, proxyManager);

                auto& materialShaderManager = context.getMaterialShaderManager();
                MaterialShader* materialShader = materialShaderManager.getMaterialShader(materialShaderHandle);

                proxyManager.createProxy(
                    handle,
                    attributes,
                    materialShader,
                    std::move(shaderMacros),
                    parameters
                );
            }
        );

        mDirtyFlags = DirtyFlags::None;
    }

    void MaterialTemplate::unrefMaterialShader()
    {
        if (!mShaderHandle.isValid())
            return;

        auto& renderFrontend = Context::get().getRenderFrontend();
        auto& materialShaderManager = renderFrontend.getMaterialShaderManager();
        materialShaderManager.freeHandle(mShaderHandle);

        renderFrontend.enqueueLambda(
            [shaderHandle = mShaderHandle](RenderFrameContext& context)
            {
                auto& materialShaderManager = context.getMaterialShaderManager();
                materialShaderManager.unrefMaterialShader(shaderHandle);
            }
        );

        // reset shader handle
        mShaderHandle = MaterialShaderHandle();
    }

    void MaterialTemplate::updateAllParameters()
    {
        MaterialProxyHandle proxyHandle = getMaterialProxyHandle();
        assert(proxyHandle.isValid());

        std::vector<MaterialProxy::Parameter> proxyParams;
        std::vector<std::pair<TextureProxyHandle, SamplerProxyHandle>> textureBindings;
        makeMaterialProxyParameters(mParameters, proxyParams, textureBindings);

        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [proxyHandle,
            parameters = std::move(proxyParams),
            textureBindings = std::move(textureBindings)
            ](RenderFrameContext& context) mutable
            {
                auto& proxyManager = context.getProxyManager();
                resolveTextureBindings(parameters, textureBindings, proxyManager);
                MaterialProxy* proxy = proxyManager.getProxy(proxyHandle);
                proxy->updateParameters(parameters);
            }
        );
    }

    void MaterialTemplate::updateMaterialProxy()
    {
        MaterialProxy::Attributes attributes;
        attributes.domain = mMaterialDomain;
        attributes.shadingModel = mShadingModel;
        attributes.blendMode = mBlendMode;
        attributes.doubleSided = mDoubleSided;

        MaterialShaderHandle materialShaderHandle = getOrCreateMaterialShader();

        std::vector<MaterialProxy::Parameter> proxyParams;
        std::vector<std::pair<TextureProxyHandle, SamplerProxyHandle>> textureBindings;
        makeMaterialProxyParameters(mParameters, proxyParams, textureBindings);

        Context::get().getRenderFrontend().enqueueLambda(
            [handle = getMaterialProxyHandle(),
            attributes,
            materialShaderHandle,
            shaderMacros = mShaderMacros,
            parameters = std::move(proxyParams),
            textureBindings = std::move(textureBindings)](RenderFrameContext& context) mutable
            {
                auto& proxyManager = context.getProxyManager();
                resolveTextureBindings(parameters, textureBindings, proxyManager);

                auto& materialShaderManager = context.getMaterialShaderManager();
                MaterialShader* materialShader = materialShaderManager.getMaterialShader(materialShaderHandle);
                MaterialProxy* materialProxy = proxyManager.getProxy(handle);
                materialProxy->update(
                    attributes,
                    materialShader,
                    std::move(shaderMacros),
                    parameters
                );
            }
        );
    }

    std::string_view getParameterTypeName(MaterialParameterType type)
    {
        switch (type)
        {
        case MaterialParameterType::Bool:  return "bool";
        case MaterialParameterType::Int: return "int";
        case MaterialParameterType::Float: return "float";
        case MaterialParameterType::Float2: return "float2";
        case MaterialParameterType::Float3: return "float3";
        case MaterialParameterType::Float4: return "float4";
        default: return "unknow";
        }
    }

    std::string MaterialTemplate::makeParameterDefines()
    {
        std::string parameterDefines;
        if (mParameters.empty())
            return parameterDefines;
        parameterDefines.reserve(1024);

        std::string macroPart;
        macroPart.reserve(512);

        parameterDefines += "struct MaterialParameters\n{\n";
        for (const auto& parameter : mParameters)
        {
            auto type = parameter.value.getType();
            auto name = parameter.name.toStringView();

            if (isTextureParameterType(type))
            {
                std::format_to(std::back_inserter(parameterDefines),
                "    uint {}TextureIndex;\n    uint {}SamplerIndex;\n", name, name);
                switch (type)
                {
                case MaterialParameterType::Texture2D:
                {
                    std::format_to(std::back_inserter(macroPart),
                        "#define {}Texture BindlessTexture2Ds[uMaterialParameters.{}TextureIndex]\n",
                        name, name
                    );
                    break;
                }
                case MaterialParameterType::Texture2DArray:
                {
                    std::format_to(std::back_inserter(macroPart),
                        "#define {}Texture BindlessTexture2DArrays[uMaterialParameters.{}TextureIndex]\n",
                        name, name
                    );
                    break;
                }
                case MaterialParameterType::Texture3D:
                {
                    std::format_to(std::back_inserter(macroPart),
                        "#define {}Texture BindlessTexture3Ds[uMaterialParameters.{}TextureIndex]\n",
                        name, name
                    );
                    break;
                }
                case MaterialParameterType::TextureCube:
                {
                    std::format_to(std::back_inserter(macroPart),
                        "#define {}Texture BindlessTextureCubes[uMaterialParameters.{}TextureIndex]\n",
                        name, name
                    );
                    break;
                }
                }
                std::format_to(std::back_inserter(macroPart),
                    "#define {}Sampler BindlessSamplers[uMaterialParameters.{}SamplerIndex]\n",
                    name, name
                );
            }
            else
            {
                const auto typeName = getParameterTypeName(type);
                std::format_to(std::back_inserter(parameterDefines),
                    "    {} {};\n", typeName, name);
                std::format_to(std::back_inserter(macroPart),
                    "#define {} uMaterialParameters.{}\n", name, name);
            }
        }
        parameterDefines += "};\n[[vk::binding(0, 2)]]\nConstantBuffer<MaterialParameters> uMaterialParameters;\n";
        parameterDefines += macroPart;

        return parameterDefines;
    }
    
    template <> Type* Reflection::createType<MaterialTemplate>()
    {
        auto c = new TClass<MaterialTemplate, Material>("MaterialTemplate");
        return c;
    }
}
