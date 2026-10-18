#include "ResourceProxy.h"
#include "RenderBackend.h"

namespace hyp
{
    ResourceProxy::~ResourceProxy() noexcept
    {
        getRenderBackend().getPrepareQueue().remove(this);
    }

    void ResourceProxy::markNeedsPrepare()
    {
        getRenderBackend().getPrepareQueue().enqueue(this);
    }
}
