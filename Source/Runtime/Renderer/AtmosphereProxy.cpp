#include "AtmosphereProxy.h"

#include <RenderBase/RenderBackend.h>

namespace hyp
{
    AtmosphereProxy::AtmosphereProxy(const AtmosphereShaderParameters& parameters) :
        mParameters(parameters)
    {
        auto& device = getRenderBackend().getDevice();
        vk::Buffer::CreateInfo bufferInfo;
        bufferInfo.size = sizeof(AtmosphereShaderParameters);
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        mParametersBuffer = new vk::Buffer(device, bufferInfo, VMA_MEMORY_USAGE_GPU_ONLY);
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

    void AtmosphereProxy::prepare(vk::CommandBuffer& cmd)
    {
        if (mParametersDirty)
        {
            uploadToGPU(cmd);
        }
    }

    void AtmosphereProxy::uploadToGPU(vk::CommandBuffer& cmd)
    {
        if (!mParametersDirty)
            return;

        cmd.updateBuffer(mParametersBuffer->getHandle(), 0, sizeof(mParameters), &mParameters);

        VkBufferMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_2_UNIFORM_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = mParametersBuffer->getHandle();
        barrier.offset = 0;
        barrier.size = sizeof(mParameters);
        cmd.pipelineBarrier2({ &barrier, 1 });

        mParametersDirty = false;
    }

}
