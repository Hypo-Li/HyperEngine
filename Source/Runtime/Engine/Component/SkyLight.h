#pragma once
#include "../Framework/Component.h"
#include "../Framework/Transform.h"
#include "../Texture/TextureCube.h"

#include <RenderBase/ProxyHandle.h>

namespace hyp
{
    enum class SkyLightSource : uint8_t
    {
        Captured_Scene,
        Environment_Map
    };

    struct SkyLightCaptureResult;

    class SkyLight : public Component, public ITransformListener
    {
        REFLECT_CLASS(SkyLight)
    public:
        SkyLight();

        ~SkyLight();

        void onRegister() override;

        void onUnregister() override;

        void onVisible() override;

        void onInvisible() override;

        void onTransformUpdate(const Transform& transform) override;

        void setLightSource(SkyLightSource lightSource);

        SkyLightSource getLightSource() const noexcept
        {
            return mLightSource;
        }

        void setEnvironmentMap(TextureCube* environmentMap);

        TextureCube* getEnvironmentMap() const noexcept
        {
            return mEnvironmentMap;
        }

        void recapture();

        void setIntensity(float intensity);

        float getIntensity() const noexcept
        {
            return mIntensity;
        }

        void setColor(float3 color);

        float3 getColor() const noexcept
        {
            return mColor;
        }

        void setPrefilterResolution(uint32_t resolution);

        uint32_t getPrefilterResolution() const noexcept
        {
            return mPrefilterResolution;
        }

        const std::array<float4, 7>& getDiffuseSHCoeff() const noexcept
        {
            return mDiffuseSHCoeff;
        }

        TextureCube* getSpecularPrefilter() const noexcept
        {
            return mSpecularPrefilter;
        }

    private:
        TRef<TextureCube> createSpecularPrefilter();

        bool isSpecularPrefilterValid(const TextureCube* texture) const noexcept;

        void requestCapture();

        void applyCaptureResult(SkyLightCaptureResult&& result, TRef<TextureCube> captureTarget);

    private:
        SkyLightSource mLightSource = SkyLightSource::Captured_Scene;
        TRef<TextureCube> mEnvironmentMap;
        std::array<float4, 7> mDiffuseSHCoeff{};
        TRef<TextureCube> mSpecularPrefilter;
        float mIntensity = 1.0f;
        float3 mColor = float3(1.0f);
        uint32_t mPrefilterResolution = 128;
        bool mRealTimeCapture = false;
        bool mRecapturePending = false;
        SkyLightProxyHandle mProxyHandle;
    };

    template <> Type* Reflection::createType<SkyLightSource>();

    template <> Type* Reflection::createType<SkyLight>();
}
