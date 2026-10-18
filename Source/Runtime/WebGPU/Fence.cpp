#include "Fence.h"

#include <chrono>
#include <thread>

namespace hyp::wgpu
{
    Fence::Fence(const Device& device)
        : rhi::Fence(device)
    {
    }

    Fence::~Fence()
    {
    }

    uint64_t Fence::getCompletedValue() const
    {
        return mCompletedValue.load(std::memory_order_acquire);
    }

    void Fence::wait(uint64_t value, uint64_t timeout)
    {
        if (mCompletedValue.load(std::memory_order_acquire) >= value)
            return;

        auto start = std::chrono::steady_clock::now();

        for (;;)
        {
            WGPUFuture future = {};
            bool hasFuture = false;

            {
                std::lock_guard lock(mMutex);
                auto it = mPendingSignals.lower_bound(value);
                if (it != mPendingSignals.end())
                {
                    future = it->second;
                    hasFuture = true;
                }
            }

            uint64_t remainingNs = std::numeric_limits<uint64_t>::max();
            if (timeout != std::numeric_limits<uint64_t>::max())
            {
                auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - start).count();
                if (elapsedNs >= static_cast<int64_t>(timeout))
                    break;
                remainingNs = timeout - static_cast<uint64_t>(elapsedNs);
            }

            if (hasFuture)
            {
                WGPUFutureWaitInfo waitInfo = {};
                waitInfo.future = future;
                waitInfo.completed = WGPU_FALSE;

                wgpuInstanceWaitAny(getWgpuDevice().getInstance(), 1, &waitInfo, remainingNs);
            }
            else
            {
                wgpuInstanceProcessEvents(getWgpuDevice().getInstance());
                std::this_thread::yield();
            }

            if (mCompletedValue.load(std::memory_order_acquire) >= value)
                break;
        }
    }

    void Fence::onQueueWorkDone(uint64_t value)
    {
        uint64_t current = mCompletedValue.load(std::memory_order_relaxed);
        while (value > current && !mCompletedValue.compare_exchange_weak(
            current, value, std::memory_order_release, std::memory_order_relaxed))
        {
        }

        {
            std::lock_guard lock(mMutex);
            mPendingSignals.erase(value);
        }
    }

    void Fence::addPendingSignal(uint64_t value, WGPUFuture future)
    {
        std::lock_guard lock(mMutex);
        mPendingSignals.emplace(value, future);
    }
}
