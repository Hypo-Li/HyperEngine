#include "PrepareQueue.h"
#include "ResourceProxy.h"

namespace hyp
{
    void PrepareQueue::enqueue(ResourceProxy* proxy)
    {
        if (proxy->mPrepareQueueIndex != ResourceProxy::kNotQueued)
            return;

        proxy->mPrepareQueueIndex = static_cast<uint32_t>(mProxies.size());
        mProxies.push_back(proxy);
    }

    void PrepareQueue::remove(ResourceProxy* proxy)
    {
        if (proxy->mPrepareQueueIndex == ResourceProxy::kNotQueued)
            return;

        mProxies[proxy->mPrepareQueueIndex] = nullptr;
        proxy->mPrepareQueueIndex = ResourceProxy::kNotQueued;
    }

    void PrepareQueue::flush(rhi::CommandList& cmdList)
    {
        size_t writeIndex = 0;
        for (size_t readIndex = 0; readIndex < mProxies.size(); ++readIndex)
        {
            ResourceProxy* proxy = mProxies[readIndex];
            if (!proxy)
                continue;

            if (proxy->prepare(cmdList))
            {
                proxy->mPrepareQueueIndex = ResourceProxy::kNotQueued;
            }
            else
            {
                proxy->mPrepareQueueIndex = static_cast<uint32_t>(writeIndex);
                mProxies[writeIndex++] = proxy;
            }
        }
        mProxies.resize(writeIndex);
    }
}
