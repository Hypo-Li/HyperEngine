#pragma once
#include "RenderableProxy.h"

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

    class SkyLightProxy : public RenderableProxy
    {
    public:
        struct CaptureRequest
        {
            std::shared_ptr<std::promise<SkyLightCaptureResult>> promise;
            TextureProxy* specularPrefilter;
        };

        SkyLightProxy(
            bool captureScene,
            TextureProxy* environmentMap,
            TextureProxy* specularPrefilter,
            double3 capturePosition,
            float intensity,
            float3 color
        );

        ~SkyLightProxy() noexcept;

        void setCaptureScene(bool captureScene) noexcept { mCaptureScene = captureScene; }

        bool isCaptureScene() const noexcept { return mCaptureScene; }

        void setEnvironmentMap(TextureProxy* environmentMap) noexcept { mEnvironmentMap = environmentMap; }

        TextureProxy* getEnvironmentMap() const noexcept { return mEnvironmentMap; }

        void setSpecularPrefilter(TextureProxy* specularPrefilter) noexcept { mSpecularPrefilter = specularPrefilter; }

        TextureProxy* getSpecularPrefilter() const noexcept { return mSpecularPrefilter; }

        void setCapturePosition(double3 capturePosition) noexcept { mCapturePosition = capturePosition; }

        double3 getCapturePosition() const noexcept { return mCapturePosition; }

        void setIntensity(float intensity) noexcept { mIntensity = intensity; }

        float getIntensity() const noexcept { return mIntensity; }

        void setColor(float3 color) noexcept { mColor = color; }

        float3 getColor() const noexcept { return mColor; }

        void requestCapture(
            std::shared_ptr<std::promise<SkyLightCaptureResult>> promise,
            TextureProxy* specularPrefilter
        );

        bool hasCaptureRequest() const noexcept { return mCaptureRequest.has_value(); }

        std::optional<CaptureRequest> consumeCaptureRequest();

        // 常驻显存的打包球谐 buffer(7 个 float4, 布局见 SkyLightPass.cpp 里原 packDiffuseSH 的注释,
        // 现在这个打包步骤挪到 SHCoeffReduce.slang 里由 GPU 完成)。SkyLightPass 每次真正 capture 时
        // 把这个 buffer import 进 render graph 当 Reduce pass 的写入目标；没有新 capture 的普通帧,
        // SkyLighting pass 直接读这里已经算好的值——不需要每帧都重新走一遍 gather+reduce
        rhi::Buffer& getPackedSHBuffer() const noexcept
        {
            return *mPackedSHBuffer;
        }

        // 场景已经有历史 capture 结果(比如反序列化恢复的 SkyLight::mDiffuseSHCoeff)、不需要重新
        // capture 时, 用这个直接把已知系数标脏, 下一次 prepare() 会自动上传到 GPU
        void setPackedSHCoeff(const std::array<float4, 7>& coeff);

        void prepare(rhi::CommandList& cmdList);

    private:
        void uploadPackedSHBuffer(rhi::CommandList& cmdList);

    private:
        bool mCaptureScene;
        TextureProxy* mEnvironmentMap;
        TextureProxy* mSpecularPrefilter;
        double3 mCapturePosition;
        float mIntensity;
        float3 mColor;
        std::optional<CaptureRequest> mCaptureRequest;
        TRef<rhi::Buffer> mPackedSHBuffer;
        std::array<float4, 7> mPackedSHCoeff{};
        bool mPackedSHCoeffDirty = false;
    };

    template <>
    struct TProxyTraits<ProxyType::Sky_Light>
    {
        using Proxy = SkyLightProxy;
    };
}
