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

        MaterialShaderHandle mShaderHandle;
    };

    template <> Type* Reflection::createType<MaterialInstance>();
}
