#pragma once
#include "InstanceProxy.h"

namespace hyp
{
    class GSplatRenderProxy : public InstanceProxy
    {
    public:
        using Desc = InstanceProxy::Desc;

        GSplatRenderProxy(SceneProxy& sceneProxy, const Desc& desc);

        ~GSplatRenderProxy() noexcept override;

        void setGSplatId(uint32_t gsplatId)
        {
            mGSplatId = gsplatId;
        }

        uint32_t getGSplatId() const
        {
            return mGSplatId;
        }

    private:
        uint32_t mGSplatId = UINT32_MAX;
    };

    template <>
    struct TProxyTraits<ProxyType::GSplat_Render>
    {
        using Proxy = GSplatRenderProxy;
    };
}
