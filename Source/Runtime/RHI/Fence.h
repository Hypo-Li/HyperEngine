#pragma once
#include <Core/Common/Refable.h>

#include <limits>

namespace hyp::rhi
{
    class Device;
    class Fence : public Refable
    {
    public:
        Fence(const Device& device) : mDevice(device) {}

        virtual ~Fence() = default;

        const Device& getDevice() const noexcept
        {
            return mDevice;
        }

        virtual uint64_t getCompletedValue() const = 0;

        // @param value to wait for
        // @param timeout in nanoseconds
        virtual void wait(uint64_t value, uint64_t timeout = std::numeric_limits<uint64_t>::max()) = 0;

    private:
        const Device& mDevice;
    };
}
