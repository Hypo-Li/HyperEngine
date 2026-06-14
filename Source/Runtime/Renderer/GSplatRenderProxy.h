#pragma once
#include "RenderableProxy.h"

namespace hyp
{
    class GSplatRenderProxy : public RenderableProxy
    {
    public:
        GSplatRenderProxy() = default;

        ~GSplatRenderProxy() noexcept = default;

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
