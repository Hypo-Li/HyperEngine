#include "Atmosphere.h"
#include "../Framework/Scene.h"
#include "../Context.h"

#include <RenderBase/RenderFrontend.h>
#include <Renderer/SceneProxy.h>
#include <Renderer/AtmosphereProxy.h>

namespace hyp
{
    REGISTER_CLASS(Atmosphere)

    Atmosphere::Atmosphere()
    {
    }

    Atmosphere::~Atmosphere() noexcept
    {
    }

    AtmosphereShaderParameters makeAtmosphereShaderParameters(const AtmosphereParameters& params)
    {
        AtmosphereShaderParameters result;
        result.rayleighScattering = params.rayleighScattering * params.rayleighScatteringScale;
        result.rayleighScaleHeight = params.rayleighScaleHeight;
        result.mieScattering = params.mieScattering * params.mieScatteringScale;
        result.mieScaleHeight = params.mieScaleHeight;
        result.mieAbsorption = params.mieAbsorption * params.mieAbsorptionScale;
        result.miePhaseG = params.mieAnisotropy;
        result.ozoneAbsorption = params.ozoneAbsorption * params.ozoneAbsorptionScale;
        result.ozoneHeight = params.ozoneHeight;
        result.ozoneThickness = params.ozoneThickness;
        result.multiScatteringFactor = params.multiScatteringFactor;
        result.bottomRadiusKm = params.groundRadiusKm;
        result.topRadiusKm = params.groundRadiusKm + params.atmosphereHeightKm;
        result.groundAlbedo = params.groundAlbedo;
        result.sun0Intensity = float3(0.0f);
        result.sun0Direction = float3(0.0f, 0.0f, 1.0f);
        result.sun1Intensity = float3(0.0f);
        result.sun1Direction = float3(0.0f, 0.0f, 1.0f);
        return result;
    }

    void Atmosphere::onRegister()
    {
        Scene* scene = getScene();
        SceneProxyHandle sceneProxyHandle = scene->getOrCreateSceneProxy();

        auto& renderFrontend = Context::get().getRenderFrontend();
        mProxyHandle = renderFrontend.allocateProxyHandle<ProxyType::Atmosphere>();
        renderFrontend.enqueueLambda(
            [sceneProxyHandle,
            proxyHandle = mProxyHandle,
            parameters = makeAtmosphereShaderParameters(mParameters)
            ](RenderFrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                AtmosphereProxy* proxy = proxyManager.createProxy(proxyHandle, parameters);
                sceneProxy->addAtmosphereProxy(proxy);
            }
        );
    }

    void Atmosphere::onUnregister()
    {
        Scene* scene = getScene();
        SceneProxyHandle sceneProxyHandle = scene->getOrCreateSceneProxy();

        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [sceneProxyHandle,
            proxyHandle = mProxyHandle
            ](RenderFrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                AtmosphereProxy* proxy = proxyManager.getProxy(proxyHandle);
                sceneProxy->removeAtmosphereProxy(proxy);
                proxyManager.destroyProxy(proxyHandle);
            }
        );

        renderFrontend.freeProxyHandle(mProxyHandle);
    }

    void Atmosphere::onTick(float deltaTime)
    {
    }

    void Atmosphere::updateParameters()
    {
        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            parameters = makeAtmosphereShaderParameters(mParameters)
            ](RenderFrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                AtmosphereProxy* proxy = proxyManager.getProxy(proxyHandle);
                proxy->updateParameters(parameters);
            }
        );
    }

    template <> Type* Reflection::createType<Atmosphere>()
    {
        auto c = new TClass<Atmosphere, Component>("Atmosphere");
        return c;
    }

}
