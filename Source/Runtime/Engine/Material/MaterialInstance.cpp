#include "MaterialInstance.h"

#include "../Context.h"
#include <RenderBase/RenderSystem.h>
#include <RenderBase/MaterialProxy.h>

namespace hyp
{
    REGISTER_CLASS(MaterialInstance)

    MaterialInstance::MaterialInstance()
    {
    }

    MaterialInstance::~MaterialInstance()
    {
        if (mTemplate)
        {
            mTemplate->unregisterInstance(this);
        }
        unrefMaterialShader();
    }

    void MaterialInstance::setTemplate(MaterialTemplate* newTemplate)
    {
        if (mTemplate)
        {
            mTemplate->unregisterInstance(this);
            mParameterOverrides.clear();
            unrefMaterialShader();
        }

        mTemplate = newTemplate;

        if (mTemplate)
        {
            mTemplate->registerInstance(this);
            reconcileParameterOverrides();
        }
    }

    bool MaterialInstance::setParameterOverriddenValue(HString name, const MaterialParameterValue& value)
    {
        auto it = mParameterOverrides.find(name);
        if (it == mParameterOverrides.end())
        {
            auto defaultValue = mTemplate->getParameterDefaultValue(name);
            if (defaultValue.has_value())
            {
                if (defaultValue->getType() != value.getType())
                    return false;
                mParameterOverrides.emplace(name, ParameterOverride{ true, value });
            }
        }
        else
        {
            ParameterOverride& parameterOverride = it->second;
            if (parameterOverride.value.getType() != value.getType())
                return false;
            parameterOverride.enabled = true;
            parameterOverride.value = value;
        }

        if (getMaterialProxyHandle().isValid())
        {
            updateParameter(name);
        }

        return true;
    }

    void MaterialInstance::setParameterOverrideEnabled(HString name, bool enabled)
    {
        auto it = mParameterOverrides.find(name);
        if (it == mParameterOverrides.end())
        {
            auto defaultValue = mTemplate->getParameterDefaultValue(name);
            if (defaultValue.has_value())
            {
                mParameterOverrides.emplace(name, ParameterOverride{ enabled, *defaultValue });
            }
        }
        else
        {
            it->second.enabled = enabled;
        }
        updateParameter(name);
    }

    std::optional<MaterialParameterValue> MaterialInstance::getParameterEffectiveValue(uint32_t index) const
    {
        auto defaultValue = mTemplate->getParameterDefaultValue(index);
        if (!defaultValue.has_value())
            return std::nullopt;

        HString name = mTemplate->getParameterName(index);
        auto it = mParameterOverrides.find(name);
        if (it != mParameterOverrides.end() && it->second.enabled)
        {
            return it->second.value;
        }
        return defaultValue;
    }

    std::optional<MaterialParameterValue> MaterialInstance::getParameterEffectiveValue(HString name) const
    {
        return getParameterEffectiveValue(mTemplate->getParameterIndex(name));
    }

    void MaterialInstance::onTemplateUpdated(MaterialTemplate::DirtyFlags dirtyFlags)
    {
        if (enumHasAnyFlags(dirtyFlags, MaterialTemplate::DirtyFlags::Pipeline))
        {
            reconcileParameterOverrides();
            unrefMaterialShader();
            updateMaterialProxy();
        }
        else if (enumHasAnyFlags(dirtyFlags, MaterialTemplate::DirtyFlags::Parameters))
        {
            updateAllParameters();
        }
    }

    std::vector<MaterialParameter> MaterialInstance::makeEffectiveParameters()
    {
        auto materialTemplateParams = mTemplate->getParameters();
        std::vector<MaterialParameter> materialInstanceParams(materialTemplateParams.begin(), materialTemplateParams.end());
        for (auto& [name, override] : mParameterOverrides)
        {
            if (override.enabled)
            {
                uint32_t index = mTemplate->getParameterIndex(name);
                materialInstanceParams[index].value = override.value;
            }
        }
        return materialInstanceParams;
    }

    void MaterialInstance::createMaterialProxy(MaterialProxyHandle handle)
    {
        assert(mTemplate);
        MaterialProxy::Attributes attributes;
        attributes.domain = mTemplate->getMaterialDomain();
        attributes.shadingModel = mTemplate->getShadingModel();
        attributes.blendMode = mTemplate->getBlendMode();
        attributes.doubleSided = mTemplate->isDoubleSided();

        mShaderHandle = mTemplate->getOrCreateMaterialShader();

        // TODO: add shader macros
        std::vector<ShaderMacro> shaderMacros;

        auto effectiveParameters = makeEffectiveParameters();
        std::vector<MaterialProxy::Parameter> proxyParams;
        std::vector<std::pair<TextureProxyHandle, SamplerProxyHandle>> textureBindings;
        makeMaterialProxyParameters(effectiveParameters, proxyParams, textureBindings);

        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [shaderHandle = mShaderHandle,
            handle,
            attributes,
            shaderMacros = std::move(shaderMacros),
            parameters = std::move(proxyParams),
            textureBindings = std::move(textureBindings)
            ](RenderFrameContext& context) mutable
            {
                auto& materialShaderManager = context.getMaterialShaderManager();
                materialShaderManager.refMaterialShader(shaderHandle);

                auto& proxyManager = context.getProxyManager();
                resolveTextureBindings(parameters, textureBindings, proxyManager);

                MaterialShader* materialShader = materialShaderManager.getMaterialShader(shaderHandle);
                proxyManager.createProxy(
                    handle,
                    attributes,
                    materialShader,
                    std::move(shaderMacros),
                    parameters
                );
            }
        );
    }

    void MaterialInstance::unrefMaterialShader()
    {
        if (!mShaderHandle.isValid())
            return;

        auto& renderFrontend = Context::get().getRenderFrontend();

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

    void MaterialInstance::updateParameter(HString parameterName)
    {
        auto effectiveValueOpt = getParameterEffectiveValue(parameterName);
        assert(effectiveValueOpt.has_value());
        auto& effectiveValue = *effectiveValueOpt;
        MaterialProxy::Parameter proxyParam;
        std::pair<TextureProxyHandle, SamplerProxyHandle> textureBinding;
        proxyParam.type = effectiveValue.getType();
        proxyParam.name = parameterName;

        auto& sampleStateManager = Context::get().getSampleStateManager();
        effectiveValue.visit(
            [&proxyParam, &textureBinding, &sampleStateManager](auto&& value)
            {
                using ValueType = std::decay_t<decltype(value)>;
                if constexpr (is_sampled_texture_v<ValueType>)
                {
                    textureBinding.first = value.texture->getOrCreateTextureProxy();
                    SampleState sampleState = value.sampleStateOverride ?
                        *value.sampleStateOverride : value.texture->getSampleState();
                    textureBinding.second = sampleStateManager.getOrCreateSamplerProxy(sampleState);
                }
                else
                {
                    proxyParam.value = value;
                }
            }
        );

        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [proxyHandle = getMaterialProxyHandle(),
            proxyParam,
            textureBinding](RenderFrameContext& context) mutable
            {
                auto& proxyManager = context.getProxyManager();
                resolveTextureBinding(proxyParam, textureBinding.first, textureBinding.second, proxyManager);
                MaterialProxy* proxy = proxyManager.getProxy(proxyHandle);
                proxy->updateParameters({ &proxyParam, 1 });
            }
        );
    }

    void MaterialInstance::updateAllParameters()
    {
        MaterialProxyHandle proxyHandle = getMaterialProxyHandle();
        if (!proxyHandle.isValid() || !mTemplate)
            return;

        auto effectiveParameters = makeEffectiveParameters();
        std::vector<MaterialProxy::Parameter> proxyParams;
        std::vector<std::pair<TextureProxyHandle, SamplerProxyHandle>> textureBindings;
        makeMaterialProxyParameters(effectiveParameters, proxyParams, textureBindings);

        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [proxyHandle,
            parameters = std::move(proxyParams),
            textureBindings = std::move(textureBindings)](RenderFrameContext& context) mutable
            {
                auto& proxyManager = context.getProxyManager();
                resolveTextureBindings(parameters, textureBindings, proxyManager);

                MaterialProxy* proxy = proxyManager.getProxy(proxyHandle);
                proxy->updateParameters(parameters);
            }
        );
    }

    void MaterialInstance::updateMaterialProxy()
    {
        MaterialProxyHandle proxyHandle = getMaterialProxyHandle();
        if (!proxyHandle.isValid() || !mTemplate)
            return;

        MaterialProxy::Attributes attributes;
        attributes.domain = mTemplate->getMaterialDomain();
        attributes.shadingModel = mTemplate->getShadingModel();
        attributes.blendMode = mTemplate->getBlendMode();
        attributes.doubleSided = mTemplate->isDoubleSided();

        mShaderHandle = mTemplate->getOrCreateMaterialShader();

        // TODO: add shader macros
        std::vector<ShaderMacro> shaderMacros;

        auto effectiveParameters = makeEffectiveParameters();
        std::vector<MaterialProxy::Parameter> proxyParams;
        std::vector<std::pair<TextureProxyHandle, SamplerProxyHandle>> textureBindings;
        makeMaterialProxyParameters(effectiveParameters, proxyParams, textureBindings);

        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [shaderHandle = mShaderHandle,
            proxyHandle,
            attributes,
            shaderMacros = std::move(shaderMacros),
            parameters = std::move(proxyParams),
            textureBindings = std::move(textureBindings)
            ](RenderFrameContext& context) mutable
            {
                auto& materialShaderManager = context.getMaterialShaderManager();
                materialShaderManager.refMaterialShader(shaderHandle);

                auto& proxyManager = context.getProxyManager();
                resolveTextureBindings(parameters, textureBindings, proxyManager);

                MaterialShader* materialShader = materialShaderManager.getMaterialShader(shaderHandle);
                MaterialProxy* proxy = proxyManager.getProxy(proxyHandle);
                proxy->update(
                    attributes,
                    materialShader,
                    std::move(shaderMacros),
                    parameters
                );
            }
        );
    }

    void MaterialInstance::reconcileParameterOverrides()
    {
        if (!mTemplate)
        {
            mParameterOverrides.clear();
            return;
        }

        std::unordered_map<HString, ParameterOverride> reconciled;
        reconciled.reserve(mParameterOverrides.size());

        for (auto& [name, override] : mParameterOverrides)
        {
            auto defaultValue = mTemplate->getParameterDefaultValue(name);
            if (!defaultValue.has_value())
            {
                // case 1：参数已被 Template 删除 → 丢弃 override
                continue;
            }

            if (defaultValue->getType() != override.value.getType())
            {
                // case 2：参数存在但类型改变 → 丢弃 override
                continue;
            }

            // case 3：参数存在且类型匹配 → 保留 override
            reconciled.emplace(name, override);
        }

        mParameterOverrides = std::move(reconciled);
    }

    template <> Type* Reflection::createType<MaterialInstance>()
    {
        auto c = new TClass<MaterialInstance, Material>("MaterialInstance");
        return c;
    }

}
