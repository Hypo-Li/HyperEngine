#include "PointLightProxy.h"
#include "SceneProxy.h"

namespace hyp
{
    PointLightProxy::PointLightProxy(SceneProxy& sceneProxy, const Desc& desc) :
        LocalLightProxy(sceneProxy, desc)
    {
        sceneProxy.registerElement(this);
        initLocalLightData(LightType::Point);
    }

    PointLightProxy::~PointLightProxy() noexcept
    {
        getSceneProxy().unregisterElement(this);
    }
}
