#include "SpotLight.h"
#include "../../Framework/Scene.h"
#include "../../Context.h"

#include <RenderBase/RenderFrontend.h>
#include <Renderer/SceneProxy.h>
#include <Renderer/SpotLightProxy.h>

namespace hyp
{
    REGISTER_CLASS(SpotLight)

    void SpotLight::onRegister()
    {
        Scene* scene = getScene();
        SceneProxyHandle sceneProxyHandle = scene->getOrCreateSceneProxy();

        Transform& transform = getEntity()->getTransform();
        auto& renderFrontend = Context::get().getRenderFrontend();
        mProxyHandle = renderFrontend.allocateProxyHandle<ProxyType::Spot_Light>();
        renderFrontend.enqueueLambda(
            [sceneProxyHandle,
            proxyHandle = mProxyHandle,
            worldMatrix = transform.getWorldMatrix(),
            position = transform.getWorldPosition(),
            attenuationRadius = mAttenuationRadius,
            sourceRadius = mSourceRadius,
            intensity = calculateIntensity(),
            castShadows = isCastShadows(),
            visible = getEntity()->isVisible(),
            innerConeAngle = mInnerConeAngle,
            outerConeAngle = mOuterConeAngle
            ](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);

                proxyManager.createProxy(proxyHandle, *sceneProxy, SpotLightProxy::Desc{
                    .local = {
                        .light = {
                            .instance = { .visible = visible, .worldMatrix = worldMatrix },
                            .intensity = intensity,
                            .castShadows = castShadows
                        },
                        .position = position,
                        .attenuationRadius = attenuationRadius,
                        .sourceRadius = sourceRadius
                    },
                    .innerConeAngle = innerConeAngle,
                    .outerConeAngle = outerConeAngle
                });
            }
        );

        scene->registerTransformListener(&transform, this);
    }

    void SpotLight::onUnregister()
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

    void SpotLight::onTick(float deltaTime)
    {
    }

    void SpotLight::onTransformUpdate(const Transform& transform)
    {
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            worldMatrix = transform.getWorldMatrix(),
            position = transform.getWorldPosition()](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SpotLightProxy* spotLightProxy = proxyManager.getProxy(proxyHandle);
                spotLightProxy->setPosition(position);
                spotLightProxy->updateInstanceTransform(worldMatrix);
            }
        );
    }

    void SpotLight::applyLightIntensity()
    {
        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            intensity = calculateIntensity()](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SpotLightProxy* spotLightProxy = proxyManager.getProxy(proxyHandle);
                spotLightProxy->setIntensity(intensity);
            }
        );
    }

    void SpotLight::applyCastShadows()
    {
        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            castShadows = isCastShadows()](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SpotLightProxy* spotLightProxy = proxyManager.getProxy(proxyHandle);
                spotLightProxy->setCastShadows(castShadows);
            }
        );
    }

    void SpotLight::onVisibilityChanged(bool visible)
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

    void SpotLight::setInnerConeAngle(float innerConeAngle)
    {
        mInnerConeAngle = innerConeAngle;
        applyConeAngles();
    }

    void SpotLight::setOuterConeAngle(float outerConeAngle)
    {
        mOuterConeAngle = outerConeAngle;
        applyConeAngles();
    }

    void SpotLight::applyConeAngles()
    {
        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            innerConeAngle = mInnerConeAngle,
            outerConeAngle = mOuterConeAngle](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SpotLightProxy* spotLightProxy = proxyManager.getProxy(proxyHandle);
                spotLightProxy->setConeAngles(innerConeAngle, outerConeAngle);
            }
        );
    }

    template <> Type* Reflection::createType<SpotLight>()
    {
        auto c = new TClass<SpotLight, LocalLight>("SpotLight");
        c->setDisplayName("Spot Light");
        c->addProperty("Inner Cone Angle", &SpotLight::mInnerConeAngle);
        c->addProperty("Outer Cone Angle", &SpotLight::mOuterConeAngle);
        return c;
    }
}
