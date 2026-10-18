#include "SkyBackgroundProxy.h"
#include "SceneProxy.h"

namespace hyp
{
    SkyBackgroundProxy::SkyBackgroundProxy(SceneProxy& sceneProxy, const Desc& desc) :
        SceneElementProxy(sceneProxy, desc.visible),
        mMaterial(desc.material),
        mIntensity(desc.intensity)
    {
        sceneProxy.registerElement(this);
    }

    SkyBackgroundProxy::~SkyBackgroundProxy() noexcept
    {
        getSceneProxy().unregisterElement(this);
    }
}
