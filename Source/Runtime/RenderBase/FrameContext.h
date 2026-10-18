#pragma once
#include "RenderBaseDefines.h"
#include "ProxyManager.h"
#include "RenderGraph/RenderGraph.h"
#include "Shader.h"

#include <RHI/Fence.h>
#include <RHI/CommandList.h>

namespace hyp
{
    class RenderBackend;
    class DeferredTaskQueue;
    class FrameContext
    {
    public:
        FrameContext(RenderBackend& renderBackend);

        void beginFrame();

        void endFrame() noexcept;

        void render(float deltaTime);

        uint2 getSwapchainExtent() const noexcept;

        // TAA jitter 用: 相机在构建 SceneView/ViewData 时(Scene.cpp 的 enqueueLambda 回调,
        // 发生在任何 RenderGraph pass 执行之前)需要一个权威的帧号来算抖动序列的下标——不能用
        // mutable 自增状态, 那样多调用一次就会让序列跑飞, 见 CameraProxy::getSceneViewGroup()
        // 的用法
        uint32_t getFrameIndex() const noexcept
        {
            return mFrameIndex;
        }

        ProxyManager& getProxyManager() noexcept;

        MaterialShaderManager& getMaterialShaderManager() noexcept;

        DeferredTaskQueue& getDeferredTaskQueue() noexcept;

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
        double mTime = 0.0;
        // TAA motion vector 用: 上一帧上传给 uFrameData 的 wrappedTime/timeWrapCount, 见
        // render() 里的更新逻辑和 FrameData::prevWrappedTime 的注释
        float mPrevWrappedTime = 0.0f;
        uint32_t mPrevTimeWrapCount = 0;
        TRef<rhi::CommandList> mCommandLists[kMaxFramesInFlight];
        TRef<rhi::Fence> mFrameFence;
        uint64_t mCurrentFenceValue = 0;
        uint64_t mLastFenceValues[kMaxFramesInFlight] = {};

        rg::TransientResources mTransientResources;
        std::unique_ptr<rg::RenderGraph> mRenderGraph;
    };
}
