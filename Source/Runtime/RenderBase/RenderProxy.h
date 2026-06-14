#pragma once
#include "ProxyHandle.h"

namespace hyp
{
    template <ProxyType PROXY_TYPE>
    struct TProxyTraits
    {
        using Proxy = void;
    };

    class RenderBackend;
    class ProxyPool;
    class RenderProxy
    {
        friend class ProxyPool;
    public:
        RenderProxy();

        virtual ~RenderProxy() noexcept = default;

        RenderBackend& getRenderBackend() const noexcept { return *mRenderBackend; }

        uint32_t getIndex() const noexcept { return mIndex; }

    private:
        RenderBackend* mRenderBackend;
        uint32_t mIndex;

        struct ConstructionContext
        {
            RenderBackend* renderBackend;
            uint32_t index;
        };
        static thread_local std::vector<ConstructionContext> sContextStack;
    };
}
