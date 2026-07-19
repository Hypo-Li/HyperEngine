#pragma once
#include <memory>
#ifndef HYP_SINGLE_THREADED_RENDERING
#include <semaphore>
#endif

#include "RenderCommandQueue.h"
#include "GlobalBindings.h"
#include "BindlessRegistry.h"
#include "DeferredTaskQueue.h"
#include "StagingAllocator.h"
#include "RenderTagRegistry.h"
#include "Shader.h"
#include "PipelineCache.h"
#include "BindGroupLayoutCache.h"
#include "SamplerCache.h"
#include "DefaultResources.h"

#include <Platform/Window.h>
#include <RHI/Context.h>

namespace hyp
{
    class RenderBackend
    {
    public:
        RenderBackend(const Window& window);

        ~RenderBackend();

        void wakeUpRenderThread() noexcept;

        void finishRenderThread();

        rhi::Backend getRHIBackend() const noexcept
        {
            return mRHIContext->getBackend();
        }

        const rhi::Device& getDevice() const noexcept
        {
            return mRHIContext->getDevice();
        }

        const Window& getWindow() const noexcept
        {
            return mWindow;
        }

        RenderCommandQueue& getCommandQueue() noexcept
        {
            return mCommandQueue;
        }

        BindGroupLayoutCache& getBindGroupLayoutCache() noexcept
        {
            return mBindGroupLayoutCache;
        }

        SamplerCache& getSamplerCache() noexcept
        {
            return mSamplerCache;
        }

        DefaultResources& getDefaultResources() noexcept
        {
            return mDefaultResources;
        }

        PipelineCache& getPipelineCache() noexcept
        {
            return mPipelineCache;
        }

        ProxyManager& getProxyManager() noexcept
        {
            return mProxyManager;
        }

        DeferredTaskQueue& getDeferredTaskQueue() noexcept
        {
            return mDeferredTaskQueue;
        }

        BindlessRegistry& getBindlessRegistry() noexcept
        {
            return mBindlessRegistry;
        }

        GlobalBindings& getGlobalBindings() noexcept
        {
            return mGlobalBindings;
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

        FrameContext& getFrameContext() noexcept
        {
            return mFrameContext;
        }

    private:
#ifndef HYP_SINGLE_THREADED_RENDERING
        void renderLoop();
#endif

    private:
        const Window& mWindow;

        std::unique_ptr<rhi::Context> mRHIContext;

        RenderCommandQueue mCommandQueue;
#ifndef HYP_SINGLE_THREADED_RENDERING
        std::thread mRenderThread;
        std::atomic_bool mRenderThreadRunning;
        std::binary_semaphore mWakeUpSignal{ 0 };
#endif

        BindGroupLayoutCache mBindGroupLayoutCache;
        SamplerCache mSamplerCache;
        DefaultResources mDefaultResources;
        PipelineCache mPipelineCache;
        ProxyManager mProxyManager;
        DeferredTaskQueue mDeferredTaskQueue;
        BindlessRegistry mBindlessRegistry;
        GlobalBindings mGlobalBindings;
        StagingAllocator mStagingAllocator;
        RenderTagRegistry mRenderTagRegistry;
        MaterialShaderManager mMaterialShaderManager;

        FrameContext mFrameContext;
    };
}
