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
        // 唤醒渲染线程进行消费
        mBackend.wakeUpRenderThread();
    }

    void RenderFrontend::beginFrame()
    {
        mFrameSemaphore.acquire();
        enqueueLambda(
            [](RenderFrameContext& context)
            {
                context.beginFrame();
            }
        );
    }

    void RenderFrontend::endFrame()
    {
        // 结束帧时，提交一个命令让渲染线程释放信号量并推进帧索引
        enqueueLambda(
            [this](RenderFrameContext& context)
            {
                context.endFrame();
                mFrameSemaphore.release();
            }
        );
        wakeUpRenderThread();
    }

    void RenderFrontend::render(double time, float deltaTime)
    {
        enqueueLambda(
            [time, deltaTime](RenderFrameContext& context)
            {
                context.render(time, deltaTime);
            }
        );
    }

    MaterialShaderManager& RenderFrontend::getMaterialShaderManager() const noexcept
    {
        return mBackend.getMaterialShaderManager();
    }

    RenderCommandQueue& RenderFrontend::getCommandQueue() noexcept
    {
        return mBackend.getCommandQueue();
    }

}
