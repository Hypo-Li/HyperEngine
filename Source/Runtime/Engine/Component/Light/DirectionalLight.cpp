#include "DirectionalLight.h"
#include "../../Framework/Scene.h"
#include "../../Context.h"

#include <RenderBase/RenderFrontend.h>
#include <Renderer/RendererContext.h>
#include <Renderer/SceneProxy.h>
#include <Renderer/DirectionalLightProxy.h>

namespace hyp
{
    REGISTER_CLASS(DirectionalLight)

    DirectionalLight::DirectionalLight()
    {
        setIntensity(6.0f);
    }

    void DirectionalLight::onRegister()
    {
        Scene* scene = getScene();
        SceneProxyHandle sceneProxyHandle = scene->getOrCreateSceneProxy();

        Transform& transform = getEntity()->getTransform();
        auto& renderFrontend = Context::get().getRenderFrontend();
        mProxyHandle = renderFrontend.allocateProxyHandle<ProxyType::Directional_Light>();
        renderFrontend.enqueueLambda(
            [sceneProxyHandle,
            proxyHandle = mProxyHandle,
            worldMatrix = transform.getWorldMatrix(),
            direction = -transform.getWorldUpVector(),
            intensity = calculateIntensity(),
            castShadows = isCastShadows(),
            visible = getEntity()->isVisible(),
            cascadeCount = mCascadeCount,
            cascadeResolution = mCascadeResolution,
            shadowDistance = mShadowDistance,
            distributionExponent = mDistributionExponent,
            depthBias = mDepthBias,
            slopeBias = mSlopeBias,
            cascadeBiasDistribution = mCascadeBiasDistribution,
            affectAtmosphere = mAffectAtmosphere,
            atmosphereSunLightIndex = mAtmosphereSunLightIndex,
            transitionFraction = mTransitionFraction,
            distanceFadeoutFraction = mDistanceFadeoutFraction,
            shadowFilterQuality = mShadowFilterQuality
            ](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);

                proxyManager.createProxy(proxyHandle, *sceneProxy, DirectionalLightProxy::Desc{
                    .light = {
                        .instance = { .visible = visible, .worldMatrix = worldMatrix },
                        .intensity = intensity,
                        .castShadows = castShadows
                    },
                    .direction = direction,
                    .cascadeCount = cascadeCount,
                    .cascadeResolution = cascadeResolution,
                    .shadowDistance = shadowDistance,
                    .distributionExponent = distributionExponent,
                    .depthBias = depthBias,
                    .slopeBias = slopeBias,
                    .cascadeBiasDistribution = cascadeBiasDistribution,
                    .affectAtmosphere = affectAtmosphere,
                    .atmosphereSunLightIndex = atmosphereSunLightIndex,
                    .transitionFraction = transitionFraction,
                    .distanceFadeoutFraction = distanceFadeoutFraction,
                    .shadowFilterQuality = shadowFilterQuality
                });
            }
        );

        scene->registerTransformListener(&transform, this);
    }

    void DirectionalLight::onUnregister()
    {
        Scene* scene = getScene();
        scene->unregisterTransformListener(&getEntity()->getTransform(), this);

        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [proxyHandle = mProxyHandle](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                DirectionalLightProxy* directionalLightProxy = proxyManager.getProxy(proxyHandle);
                auto& rendererContext = Context::get().getRendererContext();
                directionalLightProxy->clearCachedViewIds(rendererContext.getViewDataManager());

                proxyManager.destroyProxy(proxyHandle);
            }
        );

        renderFrontend.freeProxyHandle(mProxyHandle);
    }

    void DirectionalLight::onTick(float deltaTime)
    {
    }

    void DirectionalLight::onTransformUpdate(const Transform& transform)
    {
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            worldMatrix = transform.getWorldMatrix(),
            direction = -transform.getWorldUpVector()](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                DirectionalLightProxy* directionalLightProxy = proxyManager.getProxy(proxyHandle);
                directionalLightProxy->setDirection(direction);
                directionalLightProxy->updateInstanceTransform(worldMatrix);
            }
        );
    }

    void DirectionalLight::applyLightIntensity()
    {
        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            intensity = calculateIntensity()](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                DirectionalLightProxy* directionalLightProxy = proxyManager.getProxy(proxyHandle);
                directionalLightProxy->setIntensity(intensity);
            }
        );
    }

    void DirectionalLight::applyCastShadows()
    {
        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            castShadows = isCastShadows()](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                DirectionalLightProxy* directionalLightProxy = proxyManager.getProxy(proxyHandle);
                directionalLightProxy->setCastShadows(castShadows);
            }
        );
    }

    void DirectionalLight::onVisibilityChanged(bool visible)
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

    void DirectionalLight::setShadowParameters(
        uint32_t cascadeCount,
        uint32_t cascadeResolution,
        float shadowDistance,
        float distributionExponent,
        float depthBias,
        float slopeBias,
        float cascadeBiasDistribution
    )
    {
        mCascadeCount = std::clamp(cascadeCount, 1u, DirectionalLightProxy::MaxCascadeCount);
        mCascadeResolution = cascadeResolution;
        mShadowDistance = shadowDistance;
        mDistributionExponent = std::max(distributionExponent, 0.1f);
        mDepthBias = depthBias;
        mSlopeBias = slopeBias;
        mCascadeBiasDistribution = std::clamp(cascadeBiasDistribution, 0.0f, 1.0f);

        if (!mProxyHandle.isValid())
            return;
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            cascadeCount = mCascadeCount, cascadeResolution, shadowDistance,
            distributionExponent = mDistributionExponent, depthBias, slopeBias,
            cascadeBiasDistribution = mCascadeBiasDistribution
            ](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                DirectionalLightProxy* directionalLightProxy = proxyManager.getProxy(proxyHandle);
                directionalLightProxy->setShadowParameters(
                    cascadeCount,
                    cascadeResolution,
                    shadowDistance,
                    distributionExponent,
                    depthBias,
                    slopeBias,
                    cascadeBiasDistribution
                );
            }
        );
    }

    void DirectionalLight::setTransitionFraction(float transitionFraction)
    {
        mTransitionFraction = transitionFraction;
        if (!mProxyHandle.isValid())
            return;
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            transitionFraction](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                DirectionalLightProxy* directionalLightProxy = proxyManager.getProxy(proxyHandle);
                directionalLightProxy->setTransitionFraction(transitionFraction);
            }
        );
    }

    void DirectionalLight::setDistanceFadeoutFraction(float distanceFadeoutFraction)
    {
        mDistanceFadeoutFraction = distanceFadeoutFraction;
        if (!mProxyHandle.isValid())
            return;
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            distanceFadeoutFraction](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                DirectionalLightProxy* directionalLightProxy = proxyManager.getProxy(proxyHandle);
                directionalLightProxy->setDistanceFadeoutFraction(distanceFadeoutFraction);
            }
        );
    }

    void DirectionalLight::setShadowFilterQuality(ShadowFilterQuality shadowFilterQuality)
    {
        mShadowFilterQuality = shadowFilterQuality;
        if (!mProxyHandle.isValid())
            return;
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            shadowFilterQuality](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                DirectionalLightProxy* directionalLightProxy = proxyManager.getProxy(proxyHandle);
                directionalLightProxy->setShadowFilterQuality(shadowFilterQuality);
            }
        );
    }

    void DirectionalLight::setAffectAtmosphere(bool affectAtmosphere)
    {
        mAffectAtmosphere = affectAtmosphere;
        if (!mProxyHandle.isValid())
            return;
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            affectAtmosphere](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                DirectionalLightProxy* directionalLightProxy = proxyManager.getProxy(proxyHandle);
                directionalLightProxy->setAffectAtmosphere(affectAtmosphere);
            }
        );
    }

    void DirectionalLight::setAtmosphereSunLightIndex(uint32_t atmosphereSunLightIndex)
    {
        atmosphereSunLightIndex = std::min(atmosphereSunLightIndex, 1u);
        mAtmosphereSunLightIndex = atmosphereSunLightIndex;
        if (!mProxyHandle.isValid())
            return;
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            atmosphereSunLightIndex](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                DirectionalLightProxy* directionalLightProxy = proxyManager.getProxy(proxyHandle);
                directionalLightProxy->setAtmosphereSunLightIndex(atmosphereSunLightIndex);
            }
        );
    }

    template <> Type* Reflection::createType<DirectionalLight>()
    {
        auto c = new TClass<DirectionalLight, Light>("DirectionalLight");
        c->setDisplayName("Directional Light");
        return c;
    }
}
