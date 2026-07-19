#include "AtmosphereProxy.h"

#include <RenderBase/RenderBackend.h>

namespace hyp
{
    AtmosphereProxy::AtmosphereProxy(const AtmosphereShaderParameters& parameters) :
        mParameters(parameters)
    {
        auto& device = getRenderBackend().getDevice();
        rhi::BufferDesc bufferDesc;
        bufferDesc.size = sizeof(AtmosphereShaderParameters);
        bufferDesc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::Transfer_Dst;
        mParametersBuffer = device.createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);
    }

    AtmosphereProxy::~AtmosphereProxy() noexcept
    {
        getRenderBackend().getDeferredTaskQueue().push(
            [parametersBuffer = std::move(mParametersBuffer)]() mutable
            {
                parametersBuffer = nullptr;
            },
            kMaxFramesInFlight
        );
    }

    void AtmosphereProxy::updateParameters(const AtmosphereShaderParameters& parameters)
    {
        mParameters = parameters;
        mParametersDirty = true;
    }

    void AtmosphereProxy::setSunLight(uint32_t sunIndex, float3 direction, float3 intensity)
    {
        if (sunIndex == 0)
        {
            if (mParameters.sun0Direction == direction && mParameters.sun0Intensity == intensity)
                return;

            mParameters.sun0Direction = direction;
            mParameters.sun0Intensity = intensity;
        }
        else
        {
            if (mParameters.sun1Direction == direction && mParameters.sun1Intensity == intensity)
                return;

            mParameters.sun1Direction = direction;
            mParameters.sun1Intensity = intensity;
        }
        mParametersDirty = true;
    }

    void AtmosphereProxy::prepare(rhi::CommandList& cmdList)
    {
        if (mParametersDirty)
        {
            uploadToGPU(cmdList);
        }
    }

    void AtmosphereProxy::uploadToGPU(rhi::CommandList& cmdList)
    {
        if (!mParametersDirty)
            return;

        auto& stagingAllocator = getRenderBackend().getStagingAllocator();
        auto allocation = stagingAllocator.forceAllocate(sizeof(mParameters));
        std::memcpy(allocation.mappedData, &mParameters, sizeof(mParameters));

        rhi::BufferBarrier preBarrier;
        preBarrier.buffer = mParametersBuffer.get();
        preBarrier.offset = 0;
        preBarrier.size = sizeof(mParameters);
        preBarrier.oldState = rhi::ResourceState::Shader_Uniform;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({ &preBarrier, 1 });

        rhi::BufferCopyRegion copyRegion;
        copyRegion.srcOffset = allocation.offset;
        copyRegion.dstOffset = 0;
        copyRegion.size = sizeof(mParameters);
        cmdList.copyBuffer(allocation.buffer, mParametersBuffer.get(), { &copyRegion, 1 });

        rhi::BufferBarrier postBarrier;
        postBarrier.buffer = mParametersBuffer.get();
        postBarrier.offset = 0;
        postBarrier.size = sizeof(mParameters);
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Uniform;
        cmdList.insertBarriers({ &postBarrier, 1 });

        mParametersDirty = false;
    }

}
