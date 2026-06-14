#pragma once
#include "../Framework/Component.h"
#include "../Framework/Transform.h"
#include "../Texture/TextureCube.h"

namespace hyp
{
    class SkyLight : public Component, public ITransformListener
    {
    public:
        SkyLight();

        ~SkyLight();

        void onRegister() override;

        void onUnregister() override;

        void onVisible() override;

        void onInvisible() override;

        void onTransformUpdate(const Transform& transform) override;

        enum class LightSource
        {
            Captured_Scene,
            Environment_Map
        };
        void setLightSource(LightSource lightSource);

        LightSource getLightSource() const noexcept
        {
            return mLightSource;
        }

        void setEnvironmentMap(TextureCube* environmentMap);

        TextureCube* getEnvironmentMap() const noexcept
        {
            return mEnvironmentMap;
        }

        void recapture();

    private:
        LightSource mLightSource;
        TRef<TextureCube> mEnvironmentMap;
        std::array<float4, 7> mDiffuseSHCoefficients;
        TRef<TextureCube> mSpecularPrefilter;
        bool mRealTimeCapture = false;
    };
}
