#pragma once
#include "Device.h"

#include <RHI/Pipeline.h>

#include <dawn/webgpu.h>

namespace hyp::wgpu
{
    class PipelineLayout : public rhi::PipelineLayout
    {
    public:
        PipelineLayout(const Device& device, const rhi::PipelineLayoutDesc& desc);

        ~PipelineLayout() override;

        WGPUPipelineLayout getHandle() const noexcept
        {
            return mLayout;
        }

    private:
        WGPUPipelineLayout mLayout = nullptr;
    };

    class Pipeline : public rhi::Pipeline
    {
    public:
        Pipeline(const Device& device, const rhi::GraphicsPipelineDesc& desc);

        Pipeline(const Device& device, const rhi::ComputePipelineDesc& desc);

        ~Pipeline() override;

        WGPURenderPipeline getRenderPipeline() const noexcept
        {
            return mRenderPipeline;
        }

        WGPUComputePipeline getComputePipeline() const noexcept
        {
            return mComputePipeline;
        }

    private:
        WGPURenderPipeline mRenderPipeline = nullptr;
        WGPUComputePipeline mComputePipeline = nullptr;
    };
}
