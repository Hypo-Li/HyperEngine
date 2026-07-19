#include "RenderFrontend.h"
#include "RenderBackend.h"

namespace hyp
{
    RenderFrontend::RenderFrontend(RenderBackend& backend) :
        mBackend(backend)
    {

    }

    void RenderFrontend::wakeUpRenderThread()
    {
#ifndef HYP_SINGLE_THREADED_RENDERING
        // 唤醒渲染线程进行消费
        mBackend.wakeUpRenderThread();
#endif
    }

    void RenderFrontend::waitForCommandQueueSpace()
    {
#ifdef HYP_SINGLE_THREADED_RENDERING
        mBackend.getCommandQueue().executeAll(mBackend.getFrameContext());
#else
        wakeUpRenderThread();
        std::this_thread::yield();
#endif
    }

    void RenderFrontend::beginFrame()
    {
        pollFutures();
#ifndef HYP_SINGLE_THREADED_RENDERING
        mFrameSemaphore.acquire();
#endif
        enqueueLambda(
            [](FrameContext& context)
            {
                context.beginFrame();
            }
        );
    }

    void RenderFrontend::endFrame()
    {
#ifdef HYP_SINGLE_THREADED_RENDERING
        enqueueLambda(
            [](FrameContext& context)
            {
                context.endFrame();
            }
        );
        mBackend.getCommandQueue().executeAll(mBackend.getFrameContext());
#else
        // 结束帧时，提交一个命令让渲染线程释放信号量并推进帧索引
        enqueueLambda(
            [this](FrameContext& context)
            {
                context.endFrame();
                mFrameSemaphore.release();
            }
        );
        wakeUpRenderThread();
#endif
    }

    void RenderFrontend::render(double time, float deltaTime)
    {
        enqueueLambda(
            [time, deltaTime](FrameContext& context)
            {
                context.render(time, deltaTime);
            }
        );
    }

    MaterialShaderManager& RenderFrontend::getMaterialShaderManager() const noexcept
    {
        return mBackend.getMaterialShaderManager();
    }

    void RenderFrontend::pollFutures()
    {
        std::erase_if(
            mPendingFutures,
            [](auto& pendingFuture)
            {
                return pendingFuture();
            }
        );
    }

    RenderCommandQueue& RenderFrontend::getCommandQueue() noexcept
    {
        return mBackend.getCommandQueue();
    }

}
