#include "DirectionalLight.h"
#include "../Framework/Scene.h"
#include "../Context.h"

#include <RenderBase/RenderFrontend.h>
#include <Renderer/SceneProxy.h>
#include <Renderer/DirectionalLightProxy.h>

namespace hyp
{
    REGISTER_CLASS(DirectionalLight)

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
            intensity = float3(6.0f),//getColor() * getIntensity(),
            castShadows = isCastShadows(),
            cascadeCount = mCascadeCount,
            cascadeResolution = mCascadeResolution,
            shadowDistance = mShadowDistance,
            distributionExponent = mDistributionExponent,
            depthBias = mDepthBias,
            slopeBias = mSlopeBias,
            affectAtmosphere = mAffectAtmosphere,
            atmosphereSunLightIndex = mAtmosphereSunLightIndex
            ](RenderFrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);

                DirectionalLightProxy* directionalLightProxy = proxyManager.createProxy(
                    proxyHandle,
                    direction,
                    intensity,
                    castShadows,
                    cascadeCount,
                    cascadeResolution,
                    shadowDistance,
                    distributionExponent,
                    depthBias,
                    slopeBias,
                    affectAtmosphere,
                    atmosphereSunLightIndex
                );

                sceneProxy->addDirectionalLightProxy(directionalLightProxy);

                uint32_t lightIndex = directionalLightProxy->getLightIndex();
                auto& lightData = sceneProxy->getLightData(lightIndex);
                lightData.type = SceneProxy::LightType::Directional;
                lightData.direction = direction;
                lightData.intensity = intensity;
                sceneProxy->notifyLightDataUpdated(lightIndex);

                uint32_t instanceIndex = directionalLightProxy->getInstanceIndex();
                auto& instanceData = sceneProxy->getInstanceData(instanceIndex);
                instanceData.localToWorld.fromDouble4x4(worldMatrix);
                instanceData.localToWorldRot = float4x4(worldMatrix);
                sceneProxy->notifyInstanceDataUpdated(lightIndex);
            }
        );

        scene->registerTransformListener(&transform, this);
    }

    void DirectionalLight::onUnregister()
    {
        Scene* scene = getScene();
        scene->unregisterTransformListener(&getEntity()->getTransform(), this);

        SceneProxyHandle sceneProxyHandle = scene->getOrCreateSceneProxy();
        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [proxyHandle = mProxyHandle,
            sceneProxyHandle](RenderFrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                DirectionalLightProxy* directionalLightProxy = proxyManager.getProxy(proxyHandle);
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                sceneProxy->removeDirectionalLightProxy(directionalLightProxy);
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
        SceneProxyHandle sceneProxyHandle = getScene()->getOrCreateSceneProxy();
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            sceneProxyHandle,
            lightDirection = -transform.getWorldUpVector()](RenderFrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                DirectionalLightProxy* directionalLightProxy = proxyManager.getProxy(proxyHandle);
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                uint32_t lightIndex = directionalLightProxy->getLightIndex();
                auto& lightData = sceneProxy->getLightData(lightIndex);
                lightData.direction = lightDirection;
                sceneProxy->notifyLightDataUpdated(lightIndex);
            }
        );
    }

    void DirectionalLight::setAffectAtmosphere(bool affectAtmosphere)
    {
        mAffectAtmosphere = affectAtmosphere;
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            affectAtmosphere](RenderFrameContext& context)
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
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            atmosphereSunLightIndex](RenderFrameContext& context)
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
        return c;
    }
}
