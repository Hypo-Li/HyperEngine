#include "SamplerProxy.h"
#include "RenderBackend.h"

namespace hyp
{
    SamplerProxy::SamplerProxy(const vk::Sampler::CreateInfo& samplerInfo)
    {
        auto& device = getRenderBackend().getDevice();
        mSampler = device.getSamplerCache().get(samplerInfo);

        auto& bindlessRegistry = getRenderBackend().getBindlessRegistry();
        mBindlessHandle = bindlessRegistry.registerSampler(mSampler->getHandle());
    }

    SamplerProxy::~SamplerProxy() noexcept
    {
        auto& bindlessRegistry = getRenderBackend().getBindlessRegistry();

        getRenderBackend().getDeferredTaskQueue().push(
            [sampler = std::move(mSampler),
            bindlessRegistry,
            bindlessHandle = std::move(mBindlessHandle)]() mutable
            {
                bindlessRegistry.unregister(bindlessHandle);
                sampler = nullptr;
            },
            kMaxFramesInFlight
        );
    }

}
