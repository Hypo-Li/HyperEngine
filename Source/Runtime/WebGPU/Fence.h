#pragma once
#include "Device.h"

#include <RHI/Fence.h>

#include <atomic>
#include <dawn/webgpu.h>
#include <limits>
#include <map>
#include <mutex>

namespace hyp::wgpu
{
    class Fence : public rhi::Fence
    {
    public:
        Fence(const Device& device);

        ~Fence() override;

        const Device& getWgpuDevice() const noexcept
        {
            return static_cast<const Device&>(getDevice());
        }

        uint64_t getCompletedValue() const override;

        void wait(uint64_t value, uint64_t timeout = std::numeric_limits<uint64_t>::max()) override;

        void onQueueWorkDone(uint64_t value);

        void addPendingSignal(uint64_t value, WGPUFuture future);

    private:
        std::atomic<uint64_t> mCompletedValue{ 0 };
        std::mutex mMutex;
        std::map<uint64_t, WGPUFuture> mPendingSignals;
    };
}
