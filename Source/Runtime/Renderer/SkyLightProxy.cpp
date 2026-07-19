#include "SkyLightProxy.h"

#include <RenderBase/RenderBackend.h>

namespace hyp
{
    SkyLightProxy::SkyLightProxy(
        bool captureScene,
        TextureProxy* environmentMap,
        TextureProxy* specularPrefilter,
        double3 capturePosition,
        float intensity,
        float3 color
    ) : mCaptureScene(captureScene),
        mEnvironmentMap(environmentMap),
        mSpecularPrefilter(specularPrefilter),
        mCapturePosition(capturePosition),
        mIntensity(intensity),
        mColor(color)
    {
        auto& device = getRenderBackend().getDevice();
        rhi::BufferDesc bufferDesc;
        bufferDesc.size = sizeof(std::array<float4, 7>);
        // Transfer_Src 是给 SkyLightPass 的 readback pass 用的(把这个 buffer 拷到 CPU 可读的
        // staging buffer 里持久化), Transfer_Dst 是给 Reduce/上面 uploadPackedSHBuffer() 写入用的
        bufferDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Src | rhi::BufferUsage::Transfer_Dst;
        mPackedSHBuffer = device.createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);
    }

    SkyLightProxy::~SkyLightProxy() noexcept
    {
        getRenderBackend().getDeferredTaskQueue().push(
            [packedSHBuffer = std::move(mPackedSHBuffer)]() mutable
            {
                packedSHBuffer = nullptr;
            },
            kMaxFramesInFlight
        );
    }

    void SkyLightProxy::requestCapture(
        std::shared_ptr<std::promise<SkyLightCaptureResult>> promise,
        TextureProxy* specularPrefilter)
    {
        mCaptureRequest = CaptureRequest{ std::move(promise), specularPrefilter };
    }

    std::optional<SkyLightProxy::CaptureRequest> SkyLightProxy::consumeCaptureRequest()
    {
        auto result = std::move(mCaptureRequest);
        mCaptureRequest.reset();
        return result;
    }

    void SkyLightProxy::setPackedSHCoeff(const std::array<float4, 7>& coeff)
    {
        mPackedSHCoeff = coeff;
        mPackedSHCoeffDirty = true;
    }

    void SkyLightProxy::prepare(rhi::CommandList& cmdList)
    {
        if (mEnvironmentMap)
            mEnvironmentMap->prepare(cmdList);
        if (mSpecularPrefilter)
            mSpecularPrefilter->prepare(cmdList);
        if (mCaptureRequest
            && mCaptureRequest->specularPrefilter
            && mCaptureRequest->specularPrefilter != mSpecularPrefilter)
        {
            mCaptureRequest->specularPrefilter->prepare(cmdList);
        }
        if (mPackedSHCoeffDirty)
        {
            uploadPackedSHBuffer(cmdList);
        }
    }

    void SkyLightProxy::uploadPackedSHBuffer(rhi::CommandList& cmdList)
    {
        if (!mPackedSHCoeffDirty)
            return;

        auto& stagingAllocator = getRenderBackend().getStagingAllocator();
        auto allocation = stagingAllocator.forceAllocate(sizeof(mPackedSHCoeff));
        std::memcpy(allocation.mappedData, mPackedSHCoeff.data(), sizeof(mPackedSHCoeff));

        rhi::BufferBarrier preBarrier;
        preBarrier.buffer = mPackedSHBuffer.get();
        preBarrier.offset = 0;
        preBarrier.size = sizeof(mPackedSHCoeff);
        preBarrier.oldState = rhi::ResourceState::Shader_Storage;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({ &preBarrier, 1 });

        rhi::BufferCopyRegion copyRegion;
        copyRegion.srcOffset = allocation.offset;
        copyRegion.dstOffset = 0;
        copyRegion.size = sizeof(mPackedSHCoeff);
        cmdList.copyBuffer(allocation.buffer, mPackedSHBuffer.get(), { &copyRegion, 1 });

        rhi::BufferBarrier postBarrier;
        postBarrier.buffer = mPackedSHBuffer.get();
        postBarrier.offset = 0;
        postBarrier.size = sizeof(mPackedSHCoeff);
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Storage;
        cmdList.insertBarriers({ &postBarrier, 1 });

        mPackedSHCoeffDirty = false;
    }

}
