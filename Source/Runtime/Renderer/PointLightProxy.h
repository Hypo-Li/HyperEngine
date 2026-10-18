#pragma once
#include "LocalLightProxy.h"

namespace hyp
{
    class PointLightProxy : public LocalLightProxy
    {
    public:
        using Desc = LocalLightProxy::Desc;

        PointLightProxy(SceneProxy& sceneProxy, const Desc& desc);

        ~PointLightProxy() noexcept override;
    };

    template <>
    struct TProxyTraits<ProxyType::Point_Light>
    {
        using Proxy = PointLightProxy;
    };
}
