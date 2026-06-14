#pragma once
#include "RenderBaseDefines.h"
#include "ProxyManager.h"
#include "RenderGraph/RenderGraph.h"
#include "Vulkan/CommandBuffer.h"
#include "Shader.h"

namespace hyp
{
    class RenderBackend;
    class RenderFrameContext
    {
    public:
        RenderFrameContext(RenderBackend& renderBackend);

        void beginFrame();

        void endFrame() noexcept;

        void render(double time, float deltaTime);

        uint2 getSwapchainExtent() const noexcept;

        ProxyManager& getProxyManager() noexcept;

        MaterialShaderManager& getMaterialShaderManager() noexcept;

        rg::RenderGraph& getRenderGraph() noexcept
        {
            return *mRenderGraph;
        }

    private:
        bool tryResizeSwapchain();

    private:
        RenderBackend& mRenderBackend;

        TRef<vk::Surface> mSurface;
        TRef<vk::Swapchain> mSwapchain;
        bool mSkipFrame = false;

        uint32_t mFrameIndex = 0;
        uint32_t mFlightIndex = 0;
        TRef<vk::CommandBuffer> mCommandBuffers[kMaxFramesInFlight];
        std::vector<TRef<vk::BinarySemaphore>> mRenderFinishedSemaphores;
        TRef<vk::TimelineSemaphore> mFrameTimelineSemaphore;

        rg::TransientResources mTransientResources;
        std::unique_ptr<rg::RenderGraph> mRenderGraph;
    };
}
