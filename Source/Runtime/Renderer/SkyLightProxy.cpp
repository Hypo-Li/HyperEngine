#include "SkyLightProxy.h"
#include "SceneProxy.h"

#include <RenderBase/RenderBackend.h>

namespace hyp
{
    SkyLightProxy::SkyLightProxy(SceneProxy& sceneProxy, const Desc& desc) :
        SceneElementProxy(sceneProxy, desc.visible),
        mCaptureSky(desc.captureSky),
        mRealTimeCapture(desc.realTimeCapture),
        mEnvironmentMap(desc.environmentMap),
        mSpecularPrefilter(desc.specularPrefilter),
        mCapturePosition(desc.capturePosition),
        mIntensity(desc.intensity),
        mColor(desc.color)
    {
        auto& device = getRenderBackend().getDevice();
        rhi::BufferDesc bufferDesc;
        bufferDesc.size = (kRadianceScaleIndex + 1) * sizeof(float4);
        // Transfer_Src 是给 SkyLightPass 的 readback pass 用的(把这个 buffer 拷到 CPU 可读的
        // staging buffer 里持久化), Transfer_Dst 是给 Reduce/上面 uploadPackedSHBuffer() 写入用的
        bufferDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Src | rhi::BufferUsage::Transfer_Dst;
        mDataBuffer = device.createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);

        if (desc.packedSHCoeff)
            setPackedSHCoeff(*desc.packedSHCoeff);

        sceneProxy.registerElement(this);
    }

    SkyLightProxy::~SkyLightProxy() noexcept
    {
        getSceneProxy().unregisterElement(this);

        getRenderBackend().getDeferredTaskQueue().push(
            [dataBuffer = std::move(mDataBuffer)]() mutable
            {
                dataBuffer = nullptr;
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
        // 这一步发生在 render graph 的 setup 阶段, 严格早于 prepare() 所在的 execute 阶段——
        // 顺手把纹理指针存到 mPendingPrepareTexture, prepare() 才有机会在这张纹理真正被
        // SkyLightPass 的 render graph pass 用到之前, 补一次初始 layout 转换。
        if (result && result->specularPrefilter)
            mPendingPrepareTexture = result->specularPrefilter;
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
        if (mPendingPrepareTexture && mPendingPrepareTexture != mSpecularPrefilter)
        {
            mPendingPrepareTexture->prepare(cmdList);
        }
        mPendingPrepareTexture = nullptr;
        if (mPackedSHCoeffDirty)
        {
            uploadPackedSHBuffer(cmdList);
        }
        if (mRadianceScaleDirty)
        {
            uploadRadianceScale(cmdList);
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
        preBarrier.buffer = mDataBuffer.get();
        preBarrier.offset = 0;
        preBarrier.size = sizeof(mPackedSHCoeff);
        preBarrier.oldState = rhi::ResourceState::Shader_Storage;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({ &preBarrier, 1 });

        rhi::BufferCopyRegion copyRegion;
        copyRegion.srcOffset = allocation.offset;
        copyRegion.dstOffset = 0;
        copyRegion.size = sizeof(mPackedSHCoeff);
        cmdList.copyBuffer(allocation.buffer, mDataBuffer.get(), { &copyRegion, 1 });

        rhi::BufferBarrier postBarrier;
        postBarrier.buffer = mDataBuffer.get();
        postBarrier.offset = 0;
        postBarrier.size = sizeof(mPackedSHCoeff);
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Storage;
        cmdList.insertBarriers({ &postBarrier, 1 });

        mPackedSHCoeffDirty = false;
    }

    void SkyLightProxy::uploadRadianceScale(rhi::CommandList& cmdList)
    {
        constexpr uint64_t kScaleOffset = kRadianceScaleIndex * sizeof(float4);

        const float4 radianceScale(mColor * mIntensity, 0.0f);
        auto& stagingAllocator = getRenderBackend().getStagingAllocator();
        auto allocation = stagingAllocator.forceAllocate(sizeof(radianceScale));
        std::memcpy(allocation.mappedData, &radianceScale, sizeof(radianceScale));

        rhi::BufferBarrier preBarrier;
        preBarrier.buffer = mDataBuffer.get();
        preBarrier.offset = kScaleOffset;
        preBarrier.size = sizeof(radianceScale);
        preBarrier.oldState = rhi::ResourceState::Shader_Storage;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({ &preBarrier, 1 });

        rhi::BufferCopyRegion copyRegion;
        copyRegion.srcOffset = allocation.offset;
        copyRegion.dstOffset = kScaleOffset;
        copyRegion.size = sizeof(radianceScale);
        cmdList.copyBuffer(allocation.buffer, mDataBuffer.get(), { &copyRegion, 1 });

        rhi::BufferBarrier postBarrier;
        postBarrier.buffer = mDataBuffer.get();
        postBarrier.offset = kScaleOffset;
        postBarrier.size = sizeof(radianceScale);
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Storage;
        cmdList.insertBarriers({ &postBarrier, 1 });

        mRadianceScaleDirty = false;
    }

}
