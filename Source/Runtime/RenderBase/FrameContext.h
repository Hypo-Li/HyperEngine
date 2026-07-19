#pragma once
#include "RenderBaseDefines.h"
#include "ProxyManager.h"
#include "RenderGraph/RenderGraph.h"
#include "Shader.h"

#include <RHI/Fence.h>
#include <RHI/CommandList.h>

#include <functional>

namespace hyp
{
    class RenderBackend;
    class FrameContext
    {
    public:
        FrameContext(RenderBackend& renderBackend);

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
        bool tryResizeSwapchain(uint2 extent);

    private:
        RenderBackend& mRenderBackend;
        rhi::Swapchain* mSwapchain;
        uint2 mLastFramebufferSize = { 0, 0 };

        uint32_t mFrameIndex = 0;
        uint32_t mFlightIndex = 0;
        TRef<rhi::CommandList> mCommandLists[kMaxFramesInFlight];
        TRef<rhi::Fence> mFrameFence;
        uint64_t mCurrentFenceValue = 0;
        uint64_t mLastFenceValues[kMaxFramesInFlight] = {};

        rg::TransientResources mTransientResources;
        std::unique_ptr<rg::RenderGraph> mRenderGraph;
    };
}
