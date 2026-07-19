#pragma once
#include "Device.h"

#include <RHI/CommandList.h>

#include <dawn/webgpu.h>

#include <array>
#include <vector>

namespace hyp::wgpu
{
    class BindGroup;

    struct PendingBindGroup
    {
        const BindGroup* bindGroup = nullptr;
        std::vector<uint32_t> dynamicOffsets;
    };

    class CommandList : public rhi::CommandList
    {
    public:
        CommandList(const Device& device, rhi::QueueType queueType);

        ~CommandList() override = default;

        void reset() override;

        const Device& getWgpuDevice() const noexcept
        {
            return static_cast<const Device&>(getDevice());
        }

        WGPUCommandBuffer decode();

#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
#define HYP_RHI_DECLARE_BACKEND_OVERRIDE(ret, name, params) \
        ret name##Backend params override;
        HYP_RHI_COMMAND_LIST_METHODS(HYP_RHI_DECLARE_BACKEND_OVERRIDE)
#undef HYP_RHI_DECLARE_BACKEND_OVERRIDE
#else
#define HYP_RHI_DECLARE_BACKEND(ret, name, params) \
        ret name##Backend params;
        HYP_RHI_COMMAND_LIST_METHODS(HYP_RHI_DECLARE_BACKEND)
#undef HYP_RHI_DECLARE_BACKEND
#endif

    private:
        void ensureEncoder();
        void endCurrentPass();
        void releaseEncoder();
        void resetState();

        WGPUCommandEncoder mEncoder = nullptr;
        WGPURenderPassEncoder mRenderPassEncoder = nullptr;
        WGPUComputePassEncoder mComputePassEncoder = nullptr;
        std::array<PendingBindGroup, 4> mPendingGraphicsBindGroups;
        std::array<PendingBindGroup, 4> mPendingComputeBindGroups;
    };
}
