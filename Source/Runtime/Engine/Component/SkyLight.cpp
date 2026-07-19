#include "SkyLight.h"
#include "../Context.h"
#include "../Framework/Entity.h"
#include "../Framework/Scene.h"

#include <RenderBase/RenderFrontend.h>
#include <Renderer/SceneProxy.h>
#include <Renderer/SkyLightProxy.h>

#include <bit>
#include <cassert>

namespace hyp
{
    static constexpr uint32_t kMaxSkyLightPrefilterResolution = 4096;

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

        if (!isSpecularPrefilterValid(mSpecularPrefilter))
        {
            mSpecularPrefilter = createSpecularPrefilter();
            mRecapturePending = true;
        }
        else
            mSpecularPrefilter->uploadToGpu();

        TextureProxyHandle environmentMapHandle;
        if (mEnvironmentMap)
            environmentMapHandle = mEnvironmentMap->getOrCreateTextureProxy();
        TextureProxyHandle specularPrefilterHandle = mSpecularPrefilter->getOrCreateTextureProxy();

        Scene* scene = getScene();
        SceneProxyHandle sceneProxyHandle = scene->getOrCreateSceneProxy();
        Transform& transform = getEntity()->getTransform();

        auto& renderFrontend = Context::get().getRenderFrontend();
        mProxyHandle = renderFrontend.allocateProxyHandle<ProxyType::Sky_Light>();
        // mRecapturePending 为真时马上会走 requestCapture() 重新跑一遍 GPU capture 管线, 这里就不用
        // 把 mDiffuseSHCoeff 推给 proxy 了(会被这次 capture 的结果覆盖); 只有直接复用已有 prefilter
        // (比如反序列化恢复的场景)时, 才需要把已经算好的球谐系数直接上传, 不然 SkyLighting pass 在
        // 下一次 capture 发生之前读到的 PackedSHBuffer 会是全 0
        bool loadExistingSHCoeff = !mRecapturePending;
        renderFrontend.enqueueLambda(
            [sceneProxyHandle,
            proxyHandle = mProxyHandle,
            captureScene = mLightSource == SkyLightSource::Captured_Scene,
            environmentMapHandle,
            specularPrefilterHandle,
            capturePosition = transform.getWorldPosition(),
            intensity = mIntensity,
            color = mColor,
            visible = isVisible(),
            loadExistingSHCoeff,
            diffuseSHCoeff = mDiffuseSHCoeff](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                TextureProxy* environmentMap = environmentMapHandle.isValid()
                    ? proxyManager.getProxy(environmentMapHandle)
                    : nullptr;
                TextureProxy* specularPrefilter = proxyManager.getProxy(specularPrefilterHandle);

                SkyLightProxy* proxy = proxyManager.createProxy(
                    proxyHandle,
                    captureScene,
                    environmentMap,
                    specularPrefilter,
                    capturePosition,
                    intensity,
                    color
                );
                proxy->setVisible(visible);
                if (loadExistingSHCoeff)
                    proxy->setPackedSHCoeff(diffuseSHCoeff);
                sceneProxy->addSkyLightProxy(proxy);
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

        SceneProxyHandle sceneProxyHandle = scene->getOrCreateSceneProxy();
        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [sceneProxyHandle, proxyHandle = mProxyHandle](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                SkyLightProxy* proxy = proxyManager.getProxy(proxyHandle);
                sceneProxy->removeSkyLightProxy(proxy);
                proxyManager.destroyProxy(proxyHandle);
            }
        );

        renderFrontend.freeProxyHandle(mProxyHandle);
        mProxyHandle.reset();
    }

    void SkyLight::onVisible()
    {
        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle](FrameContext& context)
            {
                context.getProxyManager().getProxy(proxyHandle)->setVisible(true);
            }
        );
    }

    void SkyLight::onInvisible()
    {
        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle](FrameContext& context)
            {
                context.getProxyManager().getProxy(proxyHandle)->setVisible(false);
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

        if (!mProxyHandle.isValid())
            return;
        bool captureScene = lightSource == SkyLightSource::Captured_Scene;
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle, captureScene](FrameContext& context)
            {
                context.getProxyManager().getProxy(proxyHandle)->setCaptureScene(captureScene);
            }
        );
    }

    void SkyLight::setEnvironmentMap(TextureCube* environmentMap)
    {
        if (mEnvironmentMap == environmentMap)
            return;
        mEnvironmentMap = environmentMap;

        if (!mProxyHandle.isValid())
            return;

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
        texture->initialize(
            mPrefilterResolution,
            PixelFormat::RGBA16_SFLOAT,
            false,
            CompressionPreset::None,
            false
        );
        return texture;
    }

    bool SkyLight::isSpecularPrefilterValid(const TextureCube* texture) const noexcept
    {
        if (!texture)
            return false;

        const auto& desc = texture->getDescription();
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

        TRef<TextureCube> captureTarget = mSpecularPrefilter;
        if (!isSpecularPrefilterValid(captureTarget))
            captureTarget = createSpecularPrefilter();
        TextureProxyHandle captureTargetHandle = captureTarget->getOrCreateTextureProxy();

        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.onFutureReady(
            std::move(future),
            [skyLight = TRef<SkyLight>(this),
            captureTarget](SkyLightCaptureResult result) mutable
            {
                skyLight->applyCaptureResult(std::move(result), std::move(captureTarget));
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

    void SkyLight::applyCaptureResult(SkyLightCaptureResult&& result, TRef<TextureCube> captureTarget)
    {
        if (result.specularPrefilterMips.empty())
            return;

        mDiffuseSHCoeff = result.diffuseSHCoeff;
        captureTarget->setSourcePixels(std::move(result.specularPrefilterMips));

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
        e->addValue("Captured Scene", SkyLightSource::Captured_Scene);
        e->addValue("Environment Map", SkyLightSource::Environment_Map);
        return e;
    }

    template <> Type* Reflection::createType<SkyLight>()
    {
        auto c = new TClass<SkyLight, Component>("SkyLight");
        c->addProperty("Light Source", &SkyLight::mLightSource);
        c->addProperty("Environment Map", &SkyLight::mEnvironmentMap);
        c->addProperty("Diffuse SH Coeff", &SkyLight::mDiffuseSHCoeff);
        c->addProperty("Specular Prefilter", &SkyLight::mSpecularPrefilter);
        c->addProperty("Prefilter Resolution", &SkyLight::mPrefilterResolution);
        c->addProperty("Intensity", &SkyLight::mIntensity);
        c->addProperty("Color", &SkyLight::mColor);
        return c;
    }
}
