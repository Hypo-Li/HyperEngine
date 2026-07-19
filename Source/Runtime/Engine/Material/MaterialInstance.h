#pragma once
#include "MaterialTemplate.h"

namespace hyp
{
    class MaterialInstance : public Material
    {
        REFLECT_CLASS(MaterialInstance)
    public:
        MaterialInstance();

        ~MaterialInstance();

        void finalizeLoad() override;

        void setTemplate(MaterialTemplate* newTemplate);

        MaterialTemplate* getTemplate() const noexcept
        {
            return mTemplate;
        }

        struct ParameterOverride
        {
            bool enabled = false;
            MaterialParameterValue value;
        };

        bool setParameterOverriddenValue(HString name, const MaterialParameterValue& value);

        void setParameterOverrideEnabled(HString name, bool enabled);

        std::optional<MaterialParameterValue> getParameterEffectiveValue(uint32_t index) const;

        std::optional<MaterialParameterValue> getParameterEffectiveValue(HString name) const;

        void setBlendModeOverride(std::optional<BlendMode> blendMode) noexcept;

        std::optional<BlendMode> getBlendModeOverride() const noexcept
        {
            return mBlendModeOverride;
        }

        BlendMode getEffectiveBlendMode() const noexcept;

        void setAlphaCutoffOverride(std::optional<float> alphaCutoff) noexcept;

        std::optional<float> getAlphaCutoffOverride() const noexcept
        {
            return mAlphaCutoffOverride;
        }

        float getEffectiveAlphaCutoff() const noexcept;

        void setDoubleSidedOverride(std::optional<bool> doubleSided) noexcept;

        std::optional<bool> getDoubleSidedOverride() const noexcept
        {
            return mDoubleSidedOverride;
        }

        bool getEffectiveDoubleSided() const noexcept;

        void onTemplateUpdated(MaterialTemplate::DirtyFlags dirtyFlags);

    private:
        std::vector<MaterialParameter> makeEffectiveParameters();

        void createMaterialProxy(MaterialProxyHandle handle) override;

        void unrefMaterialShader();

        void updateParameter(HString parameterName);

        void updateAllParameters();

        void updateMaterialProxy();

        void reconcileParameterOverrides();

    private:
        TRef<MaterialTemplate> mTemplate;
        std::unordered_map<HString, ParameterOverride> mParameterOverrides;

        std::optional<BlendMode> mBlendModeOverride;
        std::optional<float> mAlphaCutoffOverride;
        std::optional<bool> mDoubleSidedOverride;

        MaterialShaderHandle mShaderHandle;
    };

    template <> Type* Reflection::createType<MaterialInstance::ParameterOverride>();

    template <> Type* Reflection::createType<MaterialInstance>();
}
