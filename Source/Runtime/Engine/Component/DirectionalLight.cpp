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
            intensity = getColor() * getIntensity(),
            castShadows = isCastShadows(),
            cascadeCount = mCascadeCount,
            cascadeResolution = mCascadeResolution,
            shadowDistance = mShadowDistance,
            distributionExponent = mDistributionExponent,
            depthBias = mDepthBias,
            slopeBias = mSlopeBias,
            affectAtmosphere = mAffectAtmosphere,
            atmosphereSunLightIndex = mAtmosphereSunLightIndex
            ](FrameContext& context)
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

                uint32_t instanceIndex = directionalLightProxy->getInstanceIndex();
                auto& instanceData = sceneProxy->getInstanceData(instanceIndex);
                instanceData.localToWorld.fromDouble4x4(worldMatrix);
                instanceData.localToWorldRot = float4x4(worldMatrix);
                sceneProxy->notifyInstanceDataUpdated(instanceIndex);

                uint32_t lightIndex = directionalLightProxy->getLightIndex();
                auto& lightData = sceneProxy->getLightData(lightIndex);
                lightData.type = SceneProxy::LightType::Directional;
                lightData.castShadows = castShadows;
                lightData.instanceIndex = instanceIndex;
                lightData.intensity = intensity;
                sceneProxy->notifyLightDataUpdated(lightIndex);
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
            sceneProxyHandle](FrameContext& context)
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
            worldMatrix = transform.getWorldMatrix(),
            direction = -transform.getWorldUpVector()](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                DirectionalLightProxy* directionalLightProxy = proxyManager.getProxy(proxyHandle);
                directionalLightProxy->setDirection(direction);

                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                uint32_t instanceIndex = directionalLightProxy->getInstanceIndex();
                auto& instanceData = sceneProxy->getInstanceData(instanceIndex);
                instanceData.localToWorld.fromDouble4x4(worldMatrix);
                instanceData.localToWorldRot = float4x4(worldMatrix);
                sceneProxy->notifyInstanceDataUpdated(instanceIndex);
            }
        );
    }

    void DirectionalLight::applyLightIntensity()
    {
        if (!mProxyHandle.isValid())
            return;

        SceneProxyHandle sceneProxyHandle = getScene()->getOrCreateSceneProxy();
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            sceneProxyHandle,
            intensity = getColor() * getIntensity()](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                DirectionalLightProxy* directionalLightProxy = proxyManager.getProxy(proxyHandle);
                directionalLightProxy->setIntensity(intensity);

                // 真正喂给 shader 的是 SceneProxy::LightData 这张共享表(所有光源类型公用),
                // 不是 DirectionalLightProxy 自己的 mIntensity——跟 onTransformUpdate() 更新
                // InstanceData 是同一个模式, 需要在这里显式写回并标脏
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                uint32_t lightIndex = directionalLightProxy->getLightIndex();
                auto& lightData = sceneProxy->getLightData(lightIndex);
                lightData.intensity = intensity;
                sceneProxy->notifyLightDataUpdated(lightIndex);
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
        return c;
    }
}
