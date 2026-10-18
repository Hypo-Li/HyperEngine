#include "LightProxy.h"
#include "SceneProxy.h"

namespace hyp
{
    LightProxy::LightProxy(SceneProxy& sceneProxy, const Desc& desc) :
        InstanceProxy(sceneProxy, desc.instance),
        mLightIndex(sceneProxy.acquireLightIndex()),
        mIntensity(desc.intensity),
        mCastShadows(desc.castShadows)
    {
    }

    void LightProxy::initLightData(LightType type) noexcept
    {
        SceneProxy& sceneProxy = getSceneProxy();
        LightData& lightData = sceneProxy.getLightData(mLightIndex);
        lightData.type = type;
        lightData.castShadows = mCastShadows;
        lightData.instanceIndex = getInstanceIndex();
        lightData.intensity = mIntensity;
        // 方向光的真值由 SceneProxy::update 每帧写回, Point/Spot 恒为无效
        lightData.shadowTileGridOffset = kInvalidSurfaceCacheAtlasPageIndex;
        sceneProxy.markLightDataUpdated(mLightIndex);
    }

    LightProxy::~LightProxy() noexcept
    {
        getSceneProxy().releaseLightIndex(mLightIndex);
    }

    void LightProxy::setIntensity(float3 intensity) noexcept
    {
        mIntensity = intensity;
        SceneProxy& sceneProxy = getSceneProxy();
        sceneProxy.getLightData(mLightIndex).intensity = mIntensity;
        sceneProxy.markLightDataUpdated(mLightIndex);
    }

    void LightProxy::setCastShadows(bool castShadows) noexcept
    {
        mCastShadows = castShadows;
        SceneProxy& sceneProxy = getSceneProxy();
        sceneProxy.getLightData(mLightIndex).castShadows = mCastShadows;
        sceneProxy.markLightDataUpdated(mLightIndex);
    }
}
