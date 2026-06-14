#pragma once
#include <semaphore>

#include "RenderCommandQueue.h"
#include "GlobalBindings.h"
#include "BindlessRegistry.h"
#include "DeferredTaskQueue.h"
#include "StagingAllocator.h"
#include "RenderTagRegistry.h"
#include "Shader.h"
#include "PipelineCache.h"

#include <Platform/Window.h>

namespace hyp
{
    class RenderBackend
    {
    public:
        RenderBackend(
            vk::Instance& instance,
            vk::Device& device,
            const Window& window
        );

        ~RenderBackend();

        void wakeUpRenderThread() noexcept;

        void finishRenderThread();

        vk::Instance& getInstance() const noexcept
        {
            return mInstance;
        }

        vk::Device& getDevice() const noexcept
        {
            return mDevice;
        }

        const Window& getWindow() const noexcept
        {
            return mWindow;
        }

        RenderCommandQueue& getCommandQueue() noexcept
        {
            return mCommandQueue;
        }

        ProxyManager& getProxyManager() noexcept
        {
            return mProxyManager;
        }

        DeferredTaskQueue& getDeferredTaskQueue() noexcept
        {
            return mDeferredTaskQueue;
        }

        GlobalBindings& getGlobalBindings() noexcept
        {
            return mGlobalBindings;
        }

        BindlessRegistry& getBindlessRegistry() noexcept
        {
            return mBindlessRegistry;
        }

        StagingAllocator& getStagingAllocator() noexcept
        {
            return mStagingAllocator;
        }

        RenderTagRegistry& getRenderTagRegistry() noexcept
        {
            return mRenderTagRegistry;
        }

        MaterialShaderManager& getMaterialShaderManager() noexcept
        {
            return mMaterialShaderManager;
        }

        PipelineCache& getPipelineCache() noexcept
        {
            return mPipelineCache;
        }

    private:
        void renderLoop();

    private:
        vk::Instance& mInstance;
        vk::Device& mDevice;
        const Window& mWindow;

        RenderCommandQueue mCommandQueue;
        std::thread mRenderThread;
        std::atomic_bool mRunning;
        std::binary_semaphore mWakeUpSignal{ 0 };

        ProxyManager mProxyManager;
        DeferredTaskQueue mDeferredTaskQueue;
        BindlessRegistry mBindlessRegistry;
        GlobalBindings mGlobalBindings;
        StagingAllocator mStagingAllocator;
        RenderTagRegistry mRenderTagRegistry;
        MaterialShaderManager mMaterialShaderManager;
        PipelineCache mPipelineCache;

        RenderFrameContext mContext;
    };
}
