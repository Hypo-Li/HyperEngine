#include "RenderProxy.h"

#include <cassert>

namespace hyp
{
    thread_local std::vector<RenderProxy::ConstructionContext> RenderProxy::sContextStack;

    RenderProxy::RenderProxy()
    {
        assert(!sContextStack.empty() && "RenderProxy must be created through ProxyManager");
        const auto& context = sContextStack.back();
        this->mRenderBackend = context.renderBackend;
        this->mIndex = context.index;
    }
}
