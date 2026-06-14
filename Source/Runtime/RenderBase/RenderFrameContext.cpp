#include "RenderFrameContext.h"
#include "RenderBackend.h"
#include "Vulkan/Queue.h"

#ifdef WIN32
#include "Vulkan/Win32Surface.h"
#endif

namespace hyp
{
    RenderFrameContext::RenderFrameContext(RenderBackend& renderBackend) :
        mRenderBackend(renderBackend),
        mTransientResources(mRenderBackend.getDevice())
    {
        auto& instance = mRenderBackend.getInstance();
        auto& device = mRenderBackend.getDevice();

#ifdef WIN32
        HINSTANCE hInstance = GetModuleHandle(nullptr);
        HWND hWnd;
        mRenderBackend.getWindow().getWin32Handle(&hWnd);
        mSurface = new vk::Win32Surface(instance, hInstance, hWnd);
#endif
        device.setupPresentQueue(mSurface->getHandle());

        mSwapchain = new vk::Swapchain(device, *mSurface);

        vk::CommandPool& gfxCmdPool = device.getGraphicsCommandPool();
        for (int i = 0; i < kMaxFramesInFlight; ++i)
        {
            mCommandBuffers[i] = gfxCmdPool.allocateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        }

        uint32_t imageCount = mSwapchain->getImageCount();
        mRenderFinishedSemaphores.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            mRenderFinishedSemaphores[i] = new vk::BinarySemaphore(device);
        }

        mFrameTimelineSemaphore = new vk::TimelineSemaphore(device);
    }

    void RenderFrameContext::beginFrame()
    {
        mRenderGraph = std::make_unique<rg::RenderGraph>(mTransientResources, *mSwapchain);
        mRenderBackend.getStagingAllocator().reclaimAllocation(mFrameTimelineSemaphore->getCounterValue());

        VkExtent2D swapchainExtent = mSwapchain->getExtent();
        VkExtent2D surfaceExtent = mSurface->queryCurrentExtent(mRenderBackend.getDevice().getPhysicalDevice());

        if (swapchainExtent.width != surfaceExtent.width ||
            swapchainExtent.height != surfaceExtent.height)
        {
            if (!tryResizeSwapchain())
                mSkipFrame = true;
        }
    }

    void RenderFrameContext::endFrame() noexcept
    {
        mRenderGraph = nullptr;

        ++mFrameIndex;
        mFlightIndex = (mFlightIndex + 1) % kMaxFramesInFlight;
        mRenderBackend.getDeferredTaskQueue().tick();
    }

    void RenderFrameContext::render(double time, float deltaTime)
    {
        if (mSkipFrame)
        {
            mSkipFrame = false;
            return;
        }

        mRenderGraph->compile();

        auto& cmd = *mCommandBuffers[mFlightIndex];
        cmd.waitSignalFence();
        uint32_t swapchainImageIndex = 0;
        VkResult acquireResult = mSwapchain->acquireNextImage(mFlightIndex, &swapchainImageIndex);
        while (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR)
        {
            if (tryResizeSwapchain())
            {
                acquireResult = mSwapchain->acquireNextImage(mFlightIndex, &swapchainImageIndex);
            }
            else
            {
                mSkipFrame = true;
                return;
            }
        }
        cmd.resetSignalFence();
        cmd.reset();

        // record
        cmd.beginRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        auto& globalBindings = mRenderBackend.getGlobalBindings();
        FrameData frameData;
        frameData.wrappedTime = static_cast<float>(std::fmod(time, 3600.0));
        frameData.timeWrapCount = static_cast<uint32_t>(time / 3600.0);
        frameData.deltaTime = deltaTime;
        frameData.frameIndex = mFrameIndex;
        globalBindings.updateFrameData(cmd, frameData);

        VkPipelineBindPoint bindPoints[] = {
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR
        };
        VkDescriptorSet descriptorSet = globalBindings.getGlobalDataDescriptorSet().getHandle();
        VkPipelineLayout pipelineLayout = globalBindings.getPipelineLayout().getHandle();
        for (auto bindPoint : bindPoints)
        {
            cmd.bindDescriptorSets(
                bindPoint,
                pipelineLayout,
                0,
                { &descriptorSet, 1 },
                {}
            );
        }

        rg::ExecuteContext executeContext(*mRenderGraph, mRenderBackend, cmd, *mSwapchain, mFrameIndex);
        mRenderGraph->execute(executeContext);

        cmd.endRecord();

        // submit
        auto& device = mRenderBackend.getDevice();
        VkSemaphore waitSemaphores[] = {
            mSwapchain->getImageAvailableSemaphore(mFlightIndex)->getHandle()
        };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        uint64_t waitValues[] = { 0 };
        VkSemaphore signalSemaphores[] = {
            mRenderFinishedSemaphores[swapchainImageIndex]->getHandle(),
            mFrameTimelineSemaphore->getHandle()
        };
        uint64_t newTimelineValue = mFrameIndex + 1;
        uint64_t signalValues[] = { 0, newTimelineValue };
        device.getGraphicsQueue().submit(
            cmd,
            waitSemaphores,
            waitStages,
            waitValues,
            signalSemaphores,
            signalValues
        );

        mRenderBackend.getStagingAllocator().commitAllocations(newTimelineValue);

        // present
        VkSemaphore presentWaitSemaphores[] = { mRenderFinishedSemaphores[swapchainImageIndex]->getHandle() };
        VkResult presentResult = device.getPresentQueue().present(
            presentWaitSemaphores,
            mSwapchain->getHandle(),
            swapchainImageIndex
        );
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
        {
            tryResizeSwapchain();
        }
    }

    uint2 RenderFrameContext::getSwapchainExtent() const noexcept
    {
        VkExtent2D extent = mSwapchain->getExtent();
        return uint2(extent.width, extent.height);
    }

    ProxyManager& RenderFrameContext::getProxyManager() noexcept
    {
        return mRenderBackend.getProxyManager();
    }

    MaterialShaderManager& RenderFrameContext::getMaterialShaderManager() noexcept
    {
        return mRenderBackend.getMaterialShaderManager();
    }

    bool RenderFrameContext::tryResizeSwapchain()
    {
        auto& device = mRenderBackend.getDevice();
        device.waitIdle();

        VkExtent2D surfaceExtent = mSurface->queryCurrentExtent(device.getPhysicalDevice());

        if (surfaceExtent.width == 0 || surfaceExtent.height == 0)
            return false;

        mSwapchain->resize();
        return true;
    }
}
