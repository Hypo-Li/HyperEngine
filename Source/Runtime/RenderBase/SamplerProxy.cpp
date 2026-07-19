#include "SamplerProxy.h"
#include "RenderBackend.h"

namespace hyp
{
    SamplerProxy::SamplerProxy(const rhi::SamplerDesc& desc)
    {
        auto& renderBackend = getRenderBackend();
        auto& device = renderBackend.getDevice();
        mSampler = renderBackend.getSamplerCache().getOrCreate(desc);

        if (device.getFeatures().bindless)
        {
            auto& bindlessRegistry = renderBackend.getBindlessRegistry();
            mBindlessHandle = bindlessRegistry.registerSampler(mSampler);
        }
    }

    SamplerProxy::~SamplerProxy() noexcept
    {
        auto& renderBackend = getRenderBackend();
        bool bindless = renderBackend.getDevice().getFeatures().bindless;

        renderBackend.getDeferredTaskQueue().push(
            [sampler = std::move(mSampler),
            bindlessRegistry = bindless ? &renderBackend.getBindlessRegistry() : nullptr,
            bindlessHandle = std::move(mBindlessHandle)]() mutable
            {
                if (bindlessRegistry && bindlessHandle.isValid())
                    bindlessRegistry->unregister(bindlessHandle);
                sampler = nullptr;
            },
            kMaxFramesInFlight
        );
    }

}
