#include "LocalLightProxy.h"
#include "SceneProxy.h"

namespace hyp
{
    LocalLightProxy::LocalLightProxy(SceneProxy& sceneProxy, const Desc& desc) :
        LightProxy(sceneProxy, desc.light),
        mPosition(desc.position),
        mAttenuationRadius(desc.attenuationRadius),
        mSourceRadius(desc.sourceRadius)
    {
    }

    void LocalLightProxy::initLocalLightData(LightType type) noexcept
    {
        initLightData(type);

        SceneProxy& sceneProxy = getSceneProxy();
        LightData& lightData = sceneProxy.getLightData(getLightIndex());
        lightData.attenuationRadius = mAttenuationRadius;
        lightData.sourceRadius = mSourceRadius;
        sceneProxy.markLightDataUpdated(getLightIndex());
    }

    void LocalLightProxy::setPosition(const double3& position) noexcept
    {
        mPosition = position;
    }

    void LocalLightProxy::setAttenuationRadius(float attenuationRadius) noexcept
    {
        mAttenuationRadius = attenuationRadius;
        SceneProxy& sceneProxy = getSceneProxy();
        sceneProxy.getLightData(getLightIndex()).attenuationRadius = mAttenuationRadius;
        sceneProxy.markLightDataUpdated(getLightIndex());
    }

    void LocalLightProxy::setSourceRadius(float sourceRadius) noexcept
    {
        mSourceRadius = sourceRadius;
        SceneProxy& sceneProxy = getSceneProxy();
        sceneProxy.getLightData(getLightIndex()).sourceRadius = mSourceRadius;
        sceneProxy.markLightDataUpdated(getLightIndex());
    }
}
