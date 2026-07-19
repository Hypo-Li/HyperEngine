#pragma once
#include "Device.h"

#include <RHI/Buffer.h>

namespace hyp::wgpu
{
    class Buffer : public rhi::Buffer
    {
    public:
        Buffer(const Device& device, const rhi::BufferDesc& desc, rhi::MemoryType memoryType);

        ~Buffer() override;

        const Device& getWgpuDevice() const noexcept
        {
            return static_cast<const Device&>(getDevice());
        }

        WGPUBuffer getHandle() const noexcept
        {
            return mBuffer;
        }

        void* map() override;

        void unmap() override;

        void setDebugName(HString name) override;

    private:
        WGPUBuffer mBuffer = nullptr;
        void* mMappedData = nullptr;
    };
}
