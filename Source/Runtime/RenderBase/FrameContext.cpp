#include "FrameContext.h"
#include "RenderBackend.h"

#include <algorithm>
#include <iterator>

namespace hyp
{
    FrameContext::FrameContext(RenderBackend& renderBackend) :
        mRenderBackend(renderBackend),
        mTransientResources(mRenderBackend.getDevice())
    {
        auto& device = mRenderBackend.getDevice();
        mSwapchain = device.getSwapchain();

        for (int i = 0; i < kMaxFramesInFlight; ++i)
        {
            mCommandLists[i] = device.createCommandList(rhi::QueueType::Graphics);
        }

        mFrameFence = device.createFence();
    }

    void FrameContext::beginFrame()
    {
        mRenderGraph = std::make_unique<rg::RenderGraph>(mTransientResources, *mSwapchain);
        mRenderBackend.getStagingAllocator().reclaimAllocations(mFrameFence->getCompletedValue());
    }

    void FrameContext::endFrame() noexcept
    {
        mRenderGraph = nullptr;
        ++mFrameIndex;
        mFlightIndex = (mFlightIndex + 1) % kMaxFramesInFlight;
        mRenderBackend.getDeferredTaskQueue().tick();
    }

    void FrameContext::render(float deltaTime)
    {
        mTime += deltaTime;

        mRenderGraph->compile();

        auto& cmdList = *mCommandLists[mFlightIndex];
        uint64_t completedValue = mLastFenceValues[mFlightIndex];
        mFrameFence->wait(completedValue);

        uint2 framebufferSize = mRenderBackend.getWindow().getFramebufferSize();
        if (framebufferSize.x == 0 || framebufferSize.y == 0)
            return;

        if (framebufferSize != mLastFramebufferSize)
        {
            mLastFramebufferSize = framebufferSize;
            tryResizeSwapchain(framebufferSize);
        }
        
        uint32_t swapchainImageIndex = 0;
        rhi::SwapchainStatus acquireResult = mSwapchain->acquireNextTexture();
        while (acquireResult == rhi::SwapchainStatus::Outdated ||
            acquireResult == rhi::SwapchainStatus::Suboptimal)
        {
            uint2 newSize = mRenderBackend.getWindow().getFramebufferSize();
            if (tryResizeSwapchain(newSize))
            {
                acquireResult = mSwapchain->acquireNextTexture();
            }
            else
            {
                return;
            }
        }
        cmdList.reset();

        // record
        // 占位 GPU 资源(白/黑纹理、DfgLut、通用占位 uniform buffer)只需要创建一次: 这里
        // cmdList 刚 reset 完、还没有任何 render/compute pass encoder 活跃, 天然满足创建过程
        // 插入 barrier 的约束, 不需要像各 Pass 自己懒加载那样单独包一个 RenderGraph TransferPass
        mRenderBackend.getDefaultResources().ensureCreated(cmdList);

        auto& globalBindings = mRenderBackend.getGlobalBindings();
        FrameData frameData;
        frameData.wrappedTime = static_cast<float>(std::fmod(mTime, 3600.0));
        frameData.timeWrapCount = static_cast<uint32_t>(mTime / 3600.0);
        frameData.deltaTime = deltaTime;
        frameData.frameIndex = mFrameIndex;
        frameData.prevWrappedTime = mPrevWrappedTime;
        frameData.prevTimeWrapCount = mPrevTimeWrapCount;
        globalBindings.updateFrameData(cmdList, frameData);
        mPrevWrappedTime = frameData.wrappedTime;
        mPrevTimeWrapCount = frameData.timeWrapCount;

        rhi::PipelineBindPoint bindPoints[] = {
            rhi::PipelineBindPoint::Graphics,
            rhi::PipelineBindPoint::Compute
        };
        for (auto bindPoint : bindPoints)
        {
            cmdList.setBindGroup(
                0,
                bindPoint,
                &globalBindings.getPipelineLayout(),
                &globalBindings.getGlobalDataBindGroup(),
                {}
            );
        }

        rg::ExecuteContext executeContext(*mRenderGraph, mRenderBackend, cmdList, *mSwapchain, mFrameIndex);
        mRenderGraph->execute(executeContext);

        if (!mRenderGraph->hasPresentPass())
        {
            rhi::TextureBarrier barrier;
            barrier.texture = &mSwapchain->getCurrentTextureView()->getTexture();
            barrier.range = mSwapchain->getCurrentTextureView()->getDesc().range;
            barrier.oldState = rhi::ResourceState::Present;
            barrier.newState = rhi::ResourceState::Present;
            cmdList.insertBarriers({}, { &barrier, 1 });
        }

        // submit
        auto& device = mRenderBackend.getDevice();
        uint64_t waitValue = mCurrentFenceValue;
        uint64_t signalValue = mCurrentFenceValue + 1;

        mRenderBackend.getStagingAllocator().commitAllocations(signalValue);

        device.submit(
            &cmdList,
            mFrameFence,
            waitValue,
            mFrameFence,
            signalValue,
            rhi::SubmitFlags::Wait_Swapchain_Image | rhi::SubmitFlags::Signal_Present_Ready
        );
        mCurrentFenceValue = signalValue;
        mLastFenceValues[mFlightIndex] = signalValue;

        // present
        rhi::SwapchainStatus presentResult = mSwapchain->present();
        if (presentResult == rhi::SwapchainStatus::Outdated ||
            presentResult == rhi::SwapchainStatus::Suboptimal)
        {
            uint2 newSize = mRenderBackend.getWindow().getFramebufferSize();
            tryResizeSwapchain(newSize);
        }
    }

    uint2 FrameContext::getSwapchainExtent() const noexcept
    {
        return mSwapchain->getExtent();
    }

    ProxyManager& FrameContext::getProxyManager() noexcept
    {
        return mRenderBackend.getProxyManager();
    }

    MaterialShaderManager& FrameContext::getMaterialShaderManager() noexcept
    {
        return mRenderBackend.getMaterialShaderManager();
    }

    DeferredTaskQueue& FrameContext::getDeferredTaskQueue() noexcept
    {
        return mRenderBackend.getDeferredTaskQueue();
    }

    bool FrameContext::tryResizeSwapchain(uint2 extent)
    {
        if (extent.x == 0 || extent.y == 0)
        {
            return false;
        }

        auto& device = mRenderBackend.getDevice();
        device.waitIdle();
        mSwapchain->resize(extent.x, extent.y);
        return true;
    }
}
