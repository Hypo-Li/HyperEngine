#pragma once
#include "Device.h"

#include <RHI/Context.h>

namespace hyp::wgpu
{
    class Context : public rhi::Context
    {
    public:
        Context(const rhi::ContextDesc& desc);

        ~Context();

        WGPUInstance getInstance() const noexcept
        {
            return mInstance;
        }

        WGPUAdapter getAdapter() const noexcept
        {
            return mAdapter;
        }

    private:
        WGPUInstance mInstance = nullptr;
        WGPUAdapter mAdapter = nullptr;
    };
}
