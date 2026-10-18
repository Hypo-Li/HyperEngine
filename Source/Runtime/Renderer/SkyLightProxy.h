#pragma once
#include "SceneElementProxy.h"

#include <Core/Math/Vector.h>
#include <RenderBase/TextureProxy.h>
#include <RHI/Buffer.h>
#include <RHI/CommandList.h>

#include <array>
#include <future>
#include <memory>
#include <optional>

namespace hyp
{
    struct SkyLightCaptureResult
    {
        std::array<float4, 7> diffuseSHCoeff{};
        std::vector<uint8_t> specularPrefilterMips;
    };

    class SkyLightProxy : public SceneElementProxy
    {
    public:
        struct CaptureRequest
        {
            std::shared_ptr<std::promise<SkyLightCaptureResult>> promise;
            TextureProxy* specularPrefilter;
        };

        struct Desc
        {
            bool visible = true;
            bool captureSky;
            bool realTimeCapture;
            TextureProxy* environmentMap;
            TextureProxy* specularPrefilter;
            double3 capturePosition;
            float intensity;
            float3 color;
            std::optional<std::array<float4, 7>> packedSHCoeff;
        };

        SkyLightProxy(SceneProxy& sceneProxy, const Desc& desc);

        ~SkyLightProxy() noexcept override;

        void setCaptureSky(bool captureSky) noexcept { mCaptureSky = captureSky; }

        bool isCaptureSky() const noexcept { return mCaptureSky; }

        // 只在 isCaptureSky() 为真时有意义——Environment_Map 来源不存在"实时"这个概念
        void setRealTimeCapture(bool realTimeCapture) noexcept { mRealTimeCapture = realTimeCapture; }

        bool isRealTimeCapture() const noexcept { return mRealTimeCapture; }

        void setEnvironmentMap(TextureProxy* environmentMap) noexcept { mEnvironmentMap = environmentMap; }

        TextureProxy* getEnvironmentMap() const noexcept { return mEnvironmentMap; }

        void setSpecularPrefilter(TextureProxy* specularPrefilter) noexcept { mSpecularPrefilter = specularPrefilter; }

        TextureProxy* getSpecularPrefilter() const noexcept { return mSpecularPrefilter; }

        void setCapturePosition(double3 capturePosition) noexcept { mCapturePosition = capturePosition; }

        double3 getCapturePosition() const noexcept { return mCapturePosition; }

        void setIntensity(float intensity) noexcept
        {
            mIntensity = intensity;
            mRadianceScaleDirty = true;
        }

        float getIntensity() const noexcept { return mIntensity; }

        void setColor(float3 color) noexcept
        {
            mColor = color;
            mRadianceScaleDirty = true;
        }

        float3 getColor() const noexcept { return mColor; }

        void requestCapture(
            std::shared_ptr<std::promise<SkyLightCaptureResult>> promise,
            TextureProxy* specularPrefilter
        );

        bool hasCaptureRequest() const noexcept { return mCaptureRequest.has_value(); }

        std::optional<CaptureRequest> consumeCaptureRequest();

        // 元素 0..6 是打包球谐系数, 元素 kRadianceScaleIndex 的 xyz 是 Color × Intensity。
        static constexpr uint32_t kRadianceScaleIndex = 7;

        // 常驻显存的天光数据 buffer(打包球谐的布局见 SkyLightPass.cpp 里原 packDiffuseSH 的注释,
        // 现在这个打包步骤挪到 SHCoeffReduce.slang 里由 GPU 完成)。SkyLightPass 每次真正 capture 时
        // 把这个 buffer import 进 render graph 当 Reduce pass 的写入目标；没有新 capture 的普通帧,
        // SkyLighting pass 直接读这里已经算好的值——不需要每帧都重新走一遍 gather+reduce
        rhi::Buffer& getDataBuffer() const noexcept
        {
            return *mDataBuffer;
        }

        // 场景已经有历史 capture 结果(比如反序列化恢复的 SkyLight::mDiffuseSHCoeff)、不需要重新
        // capture 时, 用这个直接把已知系数标脏, 下一次 prepare() 会自动上传到 GPU
        void setPackedSHCoeff(const std::array<float4, 7>& coeff);

        void prepare(rhi::CommandList& cmdList);

    private:
        void uploadPackedSHBuffer(rhi::CommandList& cmdList);

        void uploadRadianceScale(rhi::CommandList& cmdList);

    private:
        bool mCaptureSky;
        bool mRealTimeCapture;
        TextureProxy* mEnvironmentMap;
        TextureProxy* mSpecularPrefilter;
        double3 mCapturePosition;
        float mIntensity;
        float3 mColor;
        std::optional<CaptureRequest> mCaptureRequest;
        // consumeCaptureRequest() 在 render graph 的 setup 阶段(SkyLightPass::addToRenderGraph()
        // 直接同步调用, 不经过任何 pass 回调)清空 mCaptureRequest 时顺手把纹理指针存到这里——
        // setup 阶段严格早于所有 pass 的 execute 阶段, prepare() 是从某个 pass 的 execute 回调
        // 触发的, 靠这个字段才能在 mCaptureRequest 已经被清空之后, 仍然知道这张现建的纹理需要
        // 一次初始 layout 转换。
        TextureProxy* mPendingPrepareTexture = nullptr;
        TRef<rhi::Buffer> mDataBuffer;
        std::array<float4, 7> mPackedSHCoeff{};
        bool mPackedSHCoeffDirty = false;
        bool mRadianceScaleDirty = true;
    };

    template <>
    struct TProxyTraits<ProxyType::Sky_Light>
    {
        using Proxy = SkyLightProxy;
    };
}
