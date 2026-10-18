#pragma once
#include "../../Framework/Component.h"
#include "../../Material/Material.h"

#include <RenderBase/ProxyHandle.h>

namespace hyp
{
    class SkyBackground : public Component
    {
        REFLECT_CLASS(SkyBackground)
    public:
        SkyBackground();

        ~SkyBackground() noexcept;

        void onRegister() override;

        void onUnregister() override;

        void onVisibilityChanged(bool visible) override;

        void setMaterial(Material* material);

        Material* getMaterial() const noexcept
        {
            return mMaterial;
        }

        void setIntensity(float intensity);

        float getIntensity() const noexcept
        {
            return mIntensity;
        }

    private:
        TRef<Material> mMaterial;
        float mIntensity = 1.0f;

        SkyBackgroundProxyHandle mProxyHandle;
    };

    template <> Type* Reflection::createType<SkyBackground>();
}
