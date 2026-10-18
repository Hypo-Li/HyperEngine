#include "PointLight.h"
#include "../../Framework/Scene.h"
#include "../../Context.h"

#include <RenderBase/RenderFrontend.h>
#include <Renderer/SceneProxy.h>
#include <Renderer/PointLightProxy.h>

namespace hyp
{
    REGISTER_CLASS(PointLight)

    void PointLight::onRegister()
    {
        Scene* scene = getScene();
        SceneProxyHandle sceneProxyHandle = scene->getOrCreateSceneProxy();

        Transform& transform = getEntity()->getTransform();
        auto& renderFrontend = Context::get().getRenderFrontend();
        mProxyHandle = renderFrontend.allocateProxyHandle<ProxyType::Point_Light>();
        renderFrontend.enqueueLambda(
            [sceneProxyHandle,
            proxyHandle = mProxyHandle,
            worldMatrix = transform.getWorldMatrix(),
            position = transform.getWorldPosition(),
            attenuationRadius = mAttenuationRadius,
            sourceRadius = mSourceRadius,
            intensity = calculateIntensity(),
            castShadows = isCastShadows(),
            visible = getEntity()->isVisible()
            ](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);

                proxyManager.createProxy(proxyHandle, *sceneProxy, PointLightProxy::Desc{
                    .light = {
                        .instance = { .visible = visible, .worldMatrix = worldMatrix },
                        .intensity = intensity,
                        .castShadows = castShadows
                    },
                    .position = position,
                    .attenuationRadius = attenuationRadius,
                    .sourceRadius = sourceRadius
                });
            }
        );

        scene->registerTransformListener(&transform, this);
    }

    void PointLight::onUnregister()
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
    }

    void PointLight::onTick(float deltaTime)
    {
    }

    void PointLight::onTransformUpdate(const Transform& transform)
    {
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            worldMatrix = transform.getWorldMatrix(),
            position = transform.getWorldPosition()](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                PointLightProxy* pointLightProxy = proxyManager.getProxy(proxyHandle);
                pointLightProxy->setPosition(position);
                pointLightProxy->updateInstanceTransform(worldMatrix);
            }
        );
    }

    void PointLight::applyLightIntensity()
    {
        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            intensity = calculateIntensity()](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                PointLightProxy* pointLightProxy = proxyManager.getProxy(proxyHandle);
                pointLightProxy->setIntensity(intensity);
            }
        );
    }

    void PointLight::applyCastShadows()
    {
        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            castShadows = isCastShadows()](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                PointLightProxy* pointLightProxy = proxyManager.getProxy(proxyHandle);
                pointLightProxy->setCastShadows(castShadows);
            }
        );
    }

    void PointLight::onVisibilityChanged(bool visible)
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

    template <> Type* Reflection::createType<PointLight>()
    {
        auto c = new TClass<PointLight, LocalLight>("PointLight");
        c->setDisplayName("Point Light");
        return c;
    }
}
