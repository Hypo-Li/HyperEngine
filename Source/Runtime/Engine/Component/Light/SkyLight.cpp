#include "SkyLight.h"
#include "../../Context.h"
#include "../../Framework/Entity.h"
#include "../../Framework/Scene.h"

#include <RenderBase/RenderFrontend.h>
#include <Renderer/SceneProxy.h>
#include <Renderer/SkyLightProxy.h>

#include <bit>
#include <cassert>

namespace hyp
{
    static constexpr uint32_t kMaxSkyLightPrefilterResolution = 512;

    static bool isValidPrefilterResolution(uint32_t resolution)
    {
        return std::has_single_bit(resolution)
            && resolution <= kMaxSkyLightPrefilterResolution;
    }

    REGISTER_CLASS(SkyLight)

    SkyLight::SkyLight()
    {
    }

    SkyLight::~SkyLight()
    {
    }

    void SkyLight::onRegister()
    {
        mIntensity = std::max(mIntensity, 0.0f);
        mColor = glm::max(mColor, float3(0.0f));
        if (!isValidPrefilterResolution(mPrefilterResolution))
            mPrefilterResolution = 128;

        const bool effectiveRealTime = isRealTimeCapturing();
        if (effectiveRealTime)
            clearCachedPrefilterData();

        if (isSpecularPrefilterValid())
        {
            // 已经是一份有效的运行时对象(比如重复 onRegister() 但没有真正销毁重建)——
            // 直接复用, 不需要重新创建纹理、也不用碰 mCachedPrefilterData
            mSpecularPrefilter->uploadToGpu();
        }
        else
        {
            mSpecularPrefilter = createSpecularPrefilter();
            if (mCachedPrefilterData.getSize() > 0)
            {
                // 有上一次(非实时模式下)保存的 capture 结果, 直接用它重建 mip 数据, 不用重新 capture
                mCachedPrefilterData.load();
                std::span<const uint8_t> data = mCachedPrefilterData.getData();
                mSpecularPrefilter->setMips(std::vector<uint8_t>(data.begin(), data.end()));
                mCachedPrefilterData.unload();
            }
            else if (!effectiveRealTime)
            {
                // 没有缓存、也不是实时模式——发一次一次性请求补一份初始数据
                mRecapturePending = true;
            }
            // 实时模式下没有缓存也不需要做什么, 下一帧的实时 capture pass 会自动填上
        }

        TextureProxyHandle environmentMapHandle;
        if (mEnvironmentMap)
            environmentMapHandle = mEnvironmentMap->getOrCreateTextureProxy();
        TextureProxyHandle specularPrefilterHandle = mSpecularPrefilter->getOrCreateTextureProxy();

        Scene* scene = getScene();
        SceneProxyHandle sceneProxyHandle = scene->getOrCreateSceneProxy();
        Transform& transform = getEntity()->getTransform();

        auto& renderFrontend = Context::get().getRenderFrontend();
        mProxyHandle = renderFrontend.allocateProxyHandle<ProxyType::Sky_Light>();
        // mRecapturePending 为真时(非实时模式下新建/失效的 prefilter)马上会走 requestCapture()
        // 重新跑一遍 GPU capture 管线, 这里就不用把 mDiffuseSHCoeff 推给 proxy 了(会被这次 capture
        // 的结果覆盖); 实时模式同理——下一帧的实时 capture pass 会自动刷新, 也不需要现在推;
        // 只有"直接复用已有 prefilter 且非实时"(比如反序列化恢复的静态场景)时, 才需要把已经算好的
        // 球谐系数直接上传, 不然 SkyLighting pass 在下一次 capture 发生之前读到的 DataBuffer 会是全 0
        bool loadExistingSHCoeff = !mRecapturePending && !effectiveRealTime;
        renderFrontend.enqueueLambda(
            [sceneProxyHandle,
            proxyHandle = mProxyHandle,
            captureSky = mLightSource == SkyLightSource::Captured_Sky,
            realTimeCapture = mRealTimeCapture,
            environmentMapHandle,
            specularPrefilterHandle,
            capturePosition = transform.getWorldPosition(),
            intensity = mIntensity,
            color = mColor,
            visible = getEntity()->isVisible(),
            loadExistingSHCoeff,
            diffuseSHCoeff = mDiffuseSHCoeff](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                TextureProxy* environmentMap = environmentMapHandle.isValid()
                    ? proxyManager.getProxy(environmentMapHandle)
                    : nullptr;
                TextureProxy* specularPrefilter = proxyManager.getProxy(specularPrefilterHandle);

                proxyManager.createProxy(proxyHandle, *sceneProxy, SkyLightProxy::Desc{
                    .visible = visible,
                    .captureSky = captureSky,
                    .realTimeCapture = realTimeCapture,
                    .environmentMap = environmentMap,
                    .specularPrefilter = specularPrefilter,
                    .capturePosition = capturePosition,
                    .intensity = intensity,
                    .color = color,
                    .packedSHCoeff = loadExistingSHCoeff
                        ? std::optional<std::array<float4, 7>>(diffuseSHCoeff)
                        : std::nullopt
                });
            }
        );

        scene->registerTransformListener(&transform, this);

        if (mRecapturePending)
        {
            mRecapturePending = false;
            requestCapture();
        }
    }

    void SkyLight::onUnregister()
    {
        Scene* scene = getScene();
        scene->unregisterTransformListener(&getEntity()->getTransform(), this);

        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [proxyHandle = mProxyHandle](FrameContext& context)
            {
                context.getProxyManager().destroyProxy(proxyHandle);
            }
        );

        renderFrontend.freeProxyHandle(mProxyHandle);
        mProxyHandle.reset();
    }

    void SkyLight::onVisibilityChanged(bool visible)
    {
        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle, visible](FrameContext& context)
            {
                context.getProxyManager().getProxy(proxyHandle)->setVisible(visible);
            }
        );
    }

    void SkyLight::onTransformUpdate(const Transform& transform)
    {
        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            capturePosition = transform.getWorldPosition()](FrameContext& context)
            {
                context.getProxyManager().getProxy(proxyHandle)->setCapturePosition(capturePosition);
            }
        );
    }

    void SkyLight::setLightSource(SkyLightSource lightSource)
    {
        if (mLightSource == lightSource)
            return;
        mLightSource = lightSource;
        if (isRealTimeCapturing())
            clearCachedPrefilterData();

        bool captureSky = lightSource == SkyLightSource::Captured_Sky;
        if (mProxyHandle.isValid())
        {
            Context::get().getRenderFrontend().enqueueLambda(
                [proxyHandle = mProxyHandle, captureSky](FrameContext& context)
                {
                    context.getProxyManager().getProxy(proxyHandle)->setCaptureSky(captureSky);
                }
            );
        }

        // 实时 Captured Scene 模式下捕获管线本身每帧都会自动刷新, 不需要额外触发；其它情况
        // 切源之后, 旧的 prefilter/SH 数据是上一个源的结果, 对不上新源, 需要补一次 one-shot
        // capture 才会有视觉变化。
        if (!(captureSky && mRealTimeCapture))
            recapture();
    }

    void SkyLight::setRealTimeCapture(bool realTimeCapture)
    {
        if (mRealTimeCapture == realTimeCapture)
            return;
        const bool wasRealTimeCapturing = isRealTimeCapturing();
        mRealTimeCapture = realTimeCapture;
        if (isRealTimeCapturing())
            clearCachedPrefilterData();

        if (mProxyHandle.isValid())
        {
            Context::get().getRenderFrontend().enqueueLambda(
                [proxyHandle = mProxyHandle, realTimeCapture](FrameContext& context)
                {
                    context.getProxyManager().getProxy(proxyHandle)->setRealTimeCapture(realTimeCapture);
                }
            );
        }

        // 实时模式不回读, CPU 侧的球谐和缓存此时已经过期, 切回非实时要补一次
        if (wasRealTimeCapturing && !isRealTimeCapturing())
            recapture();
    }

    void SkyLight::setEnvironmentMap(TextureCube* environmentMap)
    {
        if (mEnvironmentMap == environmentMap)
            return;
        mEnvironmentMap = environmentMap;

        if (mProxyHandle.isValid())
        {
            TextureProxyHandle textureHandle;
            if (environmentMap)
                textureHandle = environmentMap->getOrCreateTextureProxy();

            Context::get().getRenderFrontend().enqueueLambda(
                [proxyHandle = mProxyHandle, textureHandle](FrameContext& context)
                {
                    auto& proxyManager = context.getProxyManager();
                    TextureProxy* textureProxy = textureHandle.isValid()
                        ? proxyManager.getProxy(textureHandle)
                        : nullptr;
                    proxyManager.getProxy(proxyHandle)->setEnvironmentMap(textureProxy);
                }
            );
        }

        // 当前源不是 Environment Map 的话, 这张贴图现在还用不上, 不需要现在就 recapture——
        // 真正切到 Environment Map 源时 setLightSource() 自己会补一次。清空成 nullptr 也要
        // recapture: 不然旧贴图 capture 出来的 prefilter 数据会一直留着, SkyLightPass 里的
        // 黑色兜底(见其注释)永远没有机会被触发。
        if (mLightSource == SkyLightSource::Environment_Map)
            recapture();
    }

    void SkyLight::recapture()
    {
        if (!mProxyHandle.isValid())
        {
            mRecapturePending = true;
            return;
        }
        requestCapture();
    }

    void SkyLight::setIntensity(float intensity)
    {
        intensity = std::max(intensity, 0.0f);
        if (mIntensity == intensity)
            return;
        mIntensity = intensity;

        if (!mProxyHandle.isValid())
            return;
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle, intensity](FrameContext& context)
            {
                context.getProxyManager().getProxy(proxyHandle)->setIntensity(intensity);
            }
        );
    }

    void SkyLight::setColor(float3 color)
    {
        color = glm::max(color, float3(0.0f));
        if (mColor == color)
            return;
        mColor = color;

        if (!mProxyHandle.isValid())
            return;
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle, color](FrameContext& context)
            {
                context.getProxyManager().getProxy(proxyHandle)->setColor(color);
            }
        );
    }

    void SkyLight::setPrefilterResolution(uint32_t resolution)
    {
        assert(isValidPrefilterResolution(resolution));
        if (!isValidPrefilterResolution(resolution) || mPrefilterResolution == resolution)
            return;

        mPrefilterResolution = resolution;
        recapture();
    }

    TRef<TextureCube> SkyLight::createSpecularPrefilter()
    {
        TRef<TextureCube> texture = createObject<TextureCube>(this);
        texture->initialize(mPrefilterResolution, PixelFormat::RGBA16_SFLOAT);
        return texture;
    }

    bool SkyLight::isSpecularPrefilterValid() const noexcept
    {
        if (!mSpecularPrefilter)
            return false;

        const auto& desc = mSpecularPrefilter->getDescription();
        return desc.width == mPrefilterResolution
            && desc.height == mPrefilterResolution
            && desc.mipLevels == std::bit_width(mPrefilterResolution)
            && desc.arrayLayers == 6
            && desc.isCube
            && desc.format == PixelFormat::RGBA16_SFLOAT;
    }

    void SkyLight::requestCapture()
    {
        auto promise = std::make_shared<std::promise<SkyLightCaptureResult>>();
        auto future = promise->get_future();
        const uint32_t serial = ++mCaptureSerial;

        TRef<TextureCube> captureTarget = mSpecularPrefilter;
        if (!isSpecularPrefilterValid())
            captureTarget = createSpecularPrefilter();
        TextureProxyHandle captureTargetHandle = captureTarget->getOrCreateTextureProxy();

        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.onFutureReady(
            std::move(future),
            [skyLight = TRef<SkyLight>(this),
            captureTarget,
            serial](SkyLightCaptureResult result) mutable
            {
                skyLight->applyCaptureResult(std::move(result), std::move(captureTarget), serial);
            }
        );

        renderFrontend.enqueueLambda(
            [proxyHandle = mProxyHandle,
            captureTargetHandle,
            promise = std::move(promise)](FrameContext& context) mutable
            {
                auto& proxyManager = context.getProxyManager();
                proxyManager.getProxy(proxyHandle)->requestCapture(
                    std::move(promise),
                    proxyManager.getProxy(captureTargetHandle)
                );
            }
        );
    }

    void SkyLight::applyCaptureResult(SkyLightCaptureResult&& result, TRef<TextureCube> captureTarget, uint32_t serial)
    {
        // 后发出的请求会取代先发出的, 过期的结果整份丢弃。
        if (serial != mCaptureSerial)
            return;

        if (result.specularPrefilterMips.empty())
            return;

        // 实时模式下这些 CPU 侧数据归实时 capture 所有, 一次性结果只负责下面的纹理替换。
        if (!isRealTimeCapturing())
        {
            mDiffuseSHCoeff = result.diffuseSHCoeff;
            mCachedPrefilterData.setData(result.specularPrefilterMips);
            captureTarget->setMips(std::move(result.specularPrefilterMips));
        }

        if (mSpecularPrefilter == captureTarget)
            return;

        // Keep the old GPU proxy alive until the render-thread pointer swap has
        // been enqueued. Its destruction command will therefore run afterwards.
        TRef<TextureCube> previousPrefilter = std::move(mSpecularPrefilter);
        mSpecularPrefilter = std::move(captureTarget);

        if (!mProxyHandle.isValid())
            return;

        TextureProxyHandle specularPrefilterHandle = mSpecularPrefilter->getOrCreateTextureProxy();
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle, specularPrefilterHandle](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                proxyManager.getProxy(proxyHandle)->setSpecularPrefilter(
                    proxyManager.getProxy(specularPrefilterHandle)
                );
            }
        );
    }

    template <> Type* Reflection::createType<SkyLightSource>()
    {
        auto e = new TEnumeration<SkyLightSource>("SkyLightSource");
        e->addValue("Captured Sky", SkyLightSource::Captured_Sky);
        e->addValue("Environment Map", SkyLightSource::Environment_Map);
        return e;
    }

    template <> Type* Reflection::createType<SkyLight>()
    {
        auto c = new TClass<SkyLight, Component>("SkyLight");
        c->setDisplayName("Sky Light");
        c->addProperty("Light Source", &SkyLight::mLightSource);
        c->addProperty("Environment Map", &SkyLight::mEnvironmentMap);
        c->addProperty("Diffuse SH Coeff", &SkyLight::mDiffuseSHCoeff);
        c->addProperty("Cached Prefilter Data", &SkyLight::mCachedPrefilterData);
        c->addProperty("Prefilter Resolution", &SkyLight::mPrefilterResolution);
        c->addProperty("Real Time Capture", &SkyLight::mRealTimeCapture);
        c->addProperty("Intensity", &SkyLight::mIntensity);
        c->addProperty("Color", &SkyLight::mColor);
        return c;
    }
}
