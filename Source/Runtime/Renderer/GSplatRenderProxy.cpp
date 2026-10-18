#include "GSplatRenderProxy.h"
#include "SceneProxy.h"

namespace hyp
{
    GSplatRenderProxy::GSplatRenderProxy(SceneProxy& sceneProxy, const Desc& desc) :
        InstanceProxy(sceneProxy, desc)
    {
        sceneProxy.registerElement(this);
    }

    GSplatRenderProxy::~GSplatRenderProxy() noexcept
    {
        SceneProxy& sceneProxy = getSceneProxy();
        sceneProxy.unregisterElement(this);
        sceneProxy.clearGSplatRenderProxySplatData(this);
    }
}
