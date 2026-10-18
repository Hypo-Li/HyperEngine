#include "MaterialInstance.h"

#include "../Config.h"
#include "../Context.h"
#include <RenderBase/RenderFrontend.h>
#include <RenderBase/MaterialProxy.h>

namespace hyp
{
    REGISTER_CLASS(MaterialInstance)

    MaterialInstance::MaterialInstance() :
        mResourceObserver(*this)
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

    void MaterialInstance::finalizeLoad()
    {
        if (mTemplate)
            mTemplate->registerInstance(this);
        syncResourceObserver();
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
        syncResourceObserver();
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
        syncResourceObserver();

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

        if (getMaterialProxyHandle().isValid())
        {
            updateParameter(name);
        }
    }

    const MaterialInstance::ParameterOverride* MaterialInstance::getParameterOverride(HString name) const
    {
        auto it = mParameterOverrides.find(name);
        return it != mParameterOverrides.end() ? &it->second : nullptr;
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

    void MaterialInstance::setBlendModeOverride(std::optional<BlendMode> blendMode) noexcept
    {
        if (mBlendModeOverride == blendMode)
            return;

        mBlendModeOverride = blendMode;
        if (getMaterialProxyHandle().isValid())
        {
            updateMaterialProxy();
        }
    }

    BlendMode MaterialInstance::getEffectiveBlendMode() const noexcept
    {
        return mBlendModeOverride.value_or(mTemplate->getBlendMode());
    }

    void MaterialInstance::setAlphaCutoffOverride(std::optional<float> alphaCutoff) noexcept
    {
        if (mAlphaCutoffOverride == alphaCutoff)
            return;

        mAlphaCutoffOverride = alphaCutoff;
        if (getMaterialProxyHandle().isValid())
        {
            updateMaterialProxy();
        }
    }

    float MaterialInstance::getEffectiveAlphaCutoff() const noexcept
    {
        return mAlphaCutoffOverride.value_or(mTemplate->getAlphaCutoff());
    }

    void MaterialInstance::setDoubleSidedOverride(std::optional<bool> doubleSided) noexcept
    {
        if (mDoubleSidedOverride == doubleSided)
            return;

        mDoubleSidedOverride = doubleSided;
        if (getMaterialProxyHandle().isValid())
        {
            updateMaterialProxy();
        }
    }

    bool MaterialInstance::getEffectiveDoubleSided() const noexcept
    {
        return mDoubleSidedOverride.value_or(mTemplate->isDoubleSided());
    }

    void MaterialInstance::onTemplateUpdated(MaterialTemplate::DirtyFlags dirtyFlags)
    {
        if (enumHasAnyFlags(dirtyFlags, MaterialTemplate::DirtyFlags::Pipeline))
        {
            reconcileParameterOverrides();
            syncResourceObserver();
            unrefMaterialShader();
            updateMaterialProxy();
        }
        else if (enumHasAnyFlags(dirtyFlags, MaterialTemplate::DirtyFlags::Parameters))
        {
            updateAllParameters();
        }
    }

    void MaterialInstance::onParameterResourceChanged()
    {
        updateAllParameters();
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

        // 私有纹理没有 override 概念, 原样从模板带过来——顺序必须跟
        // MaterialTemplate::makeParameterMetadata() 一致, shader 里的绑定槽位下标才对得上。
        auto privateTextures = mTemplate->getPrivateTextures();
        materialInstanceParams.insert(materialInstanceParams.end(), privateTextures.begin(), privateTextures.end());
        return materialInstanceParams;
    }

    void MaterialInstance::createMaterialProxy(MaterialProxyHandle handle)
    {
        assert(mTemplate);
        MaterialProxy::Attributes attributes;
        attributes.domain = mTemplate->getMaterialDomain();
        attributes.shadingModel = mTemplate->getShadingModel();
        attributes.blendMode = getEffectiveBlendMode();
        attributes.alphaCutoff = getEffectiveAlphaCutoff();
        attributes.doubleSided = getEffectiveDoubleSided();
        attributes.tangentSpaceNormal = mTemplate->isTangentSpaceNormal();
        attributes.hasVertexPositionOffset = mTemplate->hasVertexPositionOffset();
        attributes.isTimeDependent = mTemplate->isTimeDependent();

        mShaderHandle = mTemplate->getOrCreateMaterialShader();
        syncResourceObserver();

        auto effectiveParameters = makeEffectiveParameters();
        std::vector<MaterialProxy::Parameter> proxyParams;
        std::vector<std::pair<TextureProxyHandle, SamplerProxyHandle>> textureBindings;
        makeMaterialProxyParameters(effectiveParameters, proxyParams, textureBindings);

        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [shaderHandle = mShaderHandle,
            handle,
            attributes,
            parameters = std::move(proxyParams),
            textureBindings = std::move(textureBindings)
            ](FrameContext& context) mutable
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
            [shaderHandle = mShaderHandle](FrameContext& context)
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
        if (!getMaterialProxyHandle().isValid())
            return;

        auto effectiveValueOpt = getParameterEffectiveValue(parameterName);
        assert(effectiveValueOpt.has_value());
        std::vector<MaterialParameter> parameters(1);
        parameters[0].name = parameterName;
        parameters[0].value = *effectiveValueOpt;

        std::vector<MaterialProxy::Parameter> proxyParams;
        std::vector<std::pair<TextureProxyHandle, SamplerProxyHandle>> textureBindings;
        makeMaterialProxyParameters(parameters, proxyParams, textureBindings);

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = getMaterialProxyHandle(),
            proxyParams = std::move(proxyParams),
            textureBindings = std::move(textureBindings)](FrameContext& context) mutable
            {
                auto& proxyManager = context.getProxyManager();
                resolveTextureBindings(proxyParams, textureBindings, proxyManager);
                MaterialProxy* proxy = proxyManager.getProxy(proxyHandle);
                proxy->updateParameters(proxyParams);
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
            textureBindings = std::move(textureBindings)](FrameContext& context) mutable
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
        attributes.blendMode = getEffectiveBlendMode();
        attributes.alphaCutoff = getEffectiveAlphaCutoff();
        attributes.doubleSided = getEffectiveDoubleSided();
        attributes.tangentSpaceNormal = mTemplate->isTangentSpaceNormal();
        attributes.hasVertexPositionOffset = mTemplate->hasVertexPositionOffset();
        attributes.isTimeDependent = mTemplate->isTimeDependent();

        mShaderHandle = mTemplate->getOrCreateMaterialShader();

        auto effectiveParameters = makeEffectiveParameters();
        std::vector<MaterialProxy::Parameter> proxyParams;
        std::vector<std::pair<TextureProxyHandle, SamplerProxyHandle>> textureBindings;
        makeMaterialProxyParameters(effectiveParameters, proxyParams, textureBindings);

        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [shaderHandle = mShaderHandle,
            proxyHandle,
            attributes,
            parameters = std::move(proxyParams),
            textureBindings = std::move(textureBindings)
            ](FrameContext& context) mutable
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
                    parameters
                );
            }
        );
    }

    void MaterialInstance::syncResourceObserver()
    {
        std::vector<MaterialParameterValue> values;
        values.reserve(mParameterOverrides.size());
        for (const auto& [name, parameterOverride] : mParameterOverrides)
            values.push_back(parameterOverride.value);
        mResourceObserver.sync(values);
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

    template <> Type* Reflection::createType<MaterialInstance::ParameterOverride>()
    {
        auto s = new TStructure<MaterialInstance::ParameterOverride>("MaterialInstance.ParameterOverride");
        s->addProperty("Enabled", &MaterialInstance::ParameterOverride::enabled);
        s->addProperty("Value", &MaterialInstance::ParameterOverride::value);
        return s;
    }

    template <> Type* Reflection::createType<MaterialInstance>()
    {
        auto c = new TClass<MaterialInstance, Material>("MaterialInstance");
        c->setDisplayName("Material Instance");
        c->addProperty("Template", &MaterialInstance::mTemplate);
        c->addProperty("Parameter Overrides", &MaterialInstance::mParameterOverrides);
        c->addProperty("Blend Mode Override", &MaterialInstance::mBlendModeOverride);
        c->addProperty("Alpha Cutoff Override", &MaterialInstance::mAlphaCutoffOverride);
        c->addProperty("Double Sided Override", &MaterialInstance::mDoubleSidedOverride);
        return c;
    }

}
