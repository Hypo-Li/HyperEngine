#include "RenderBackend.h"

namespace hyp
{
    RenderBackend::RenderBackend(
        vk::Instance& instance,
        vk::Device& device,
        const Window& window
    ) : mInstance(instance),
        mDevice(device),
        mWindow(window),
        mCommandQueue(1024 * 1024), // 1MB 的 Command Buffer
        mRunning(true),
        mProxyManager(*this),
        mBindlessRegistry(BindlessConfig{}),
        mGlobalBindings(device, mBindlessRegistry),
        mStagingAllocator(device, 64 * 1024 * 1024), // 64MB 的 Staging Ring Buffer
        mRenderTagRegistry(),
        mMaterialShaderManager(device, mRenderTagRegistry),
        mPipelineCache(device),
        mContext(*this)
    {
        mRenderThread = std::thread(&RenderBackend::renderLoop, this);
    }

    RenderBackend::~RenderBackend()
    {
        finishRenderThread();
    }

    void RenderBackend::wakeUpRenderThread() noexcept
    {
        mWakeUpSignal.release();
    }

    void RenderBackend::finishRenderThread()
    {
        mRunning = false;
        mWakeUpSignal.release();
        if (mRenderThread.joinable())
            mRenderThread.join();
    }

    void RenderBackend::renderLoop()
    {
        while (mRunning)
        {
            mWakeUpSignal.acquire();
            if (!mRunning)
                break;
            mCommandQueue.executeAll(mContext);
        }
        mCommandQueue.executeAll(mContext);
        mDevice.waitIdle();
        mDeferredTaskQueue.flush();
    }

}
