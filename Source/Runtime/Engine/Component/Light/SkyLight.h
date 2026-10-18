#pragma once
#include "../../Framework/Component.h"
#include "../../Framework/Transform.h"
#include "../../Texture/TextureCube.h"

#include <Core/Common/BulkData.h>
#include <RenderBase/ProxyHandle.h>

namespace hyp
{
    enum class SkyLightSource : uint8_t
    {
        Captured_Sky,
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

        void onVisibilityChanged(bool visible) override;

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

        // 只在 getLightSource() == Captured_Sky 时有效——每帧全量重新 capture,
        // 不回读、不更新 mDiffuseSHCoeff/specular prefilter 的 CPU 侧数据
        void setRealTimeCapture(bool realTimeCapture);

        bool isRealTimeCapture() const noexcept
        {
            return mRealTimeCapture;
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

        // 检查的是当前的 mSpecularPrefilter, 不接受别的纹理——两处调用点(onRegister()、
        // requestCapture())传进来的从来都只会是 mSpecularPrefilter 本身, 不需要参数化
        bool isSpecularPrefilterValid() const noexcept;

        bool isRealTimeCapturing() const noexcept
        {
            return mLightSource == SkyLightSource::Captured_Sky && mRealTimeCapture;
        }

        void clearCachedPrefilterData() noexcept
        {
            mCachedPrefilterData = BulkData();
        }

        void requestCapture();

        void applyCaptureResult(SkyLightCaptureResult&& result, TRef<TextureCube> captureTarget, uint32_t serial);

    private:
        SkyLightSource mLightSource = SkyLightSource::Captured_Sky;
        TRef<TextureCube> mEnvironmentMap;
        std::array<float4, 7> mDiffuseSHCoeff{};
        BulkData mCachedPrefilterData;
        float mIntensity = 1.0f;
        float3 mColor = float3(1.0f);
        uint32_t mPrefilterResolution = 128;
        bool mRealTimeCapture = true;
        bool mRecapturePending = false;
        uint32_t mCaptureSerial = 0;

        SkyLightProxyHandle mProxyHandle;
        // mSpecularPrefilter 本身不序列化(它是运行时持有 GPU 纹理资源的对象, 不是可以直接
        // 序列化的资产)——真正持久化的是这份原始字节, onRegister()/postLoad 时据此重建
        // mSpecularPrefilter 的 mip 数据, 不需要重新 capture
        TRef<TextureCube> mSpecularPrefilter;
    };

    template <> Type* Reflection::createType<SkyLightSource>();

    template <> Type* Reflection::createType<SkyLight>();
}
