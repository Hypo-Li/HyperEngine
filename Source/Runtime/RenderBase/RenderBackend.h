#pragma once
#include "RenderCommandQueue.h"
#include "GlobalBindings.h"
#include "BindlessRegistry.h"
#include "DeferredTaskQueue.h"
#include "PrepareQueue.h"
#include "StagingAllocator.h"
#include "RenderTagRegistry.h"
#include "Shader.h"
#include "PipelineCache.h"
#include "BindGroupLayoutCache.h"
#include "SamplerCache.h"
#include "DefaultResources.h"
#include "DistanceFieldBrickAtlas.h"
#include "RangeTable.h"
#include "TextureClearer.h"

#include <Platform/Window.h>
#include <RHI/Context.h>

#include <filesystem>
#include <memory>
#ifndef HYP_SINGLE_THREADED_RENDERING
#include <semaphore>
#include <thread>
#endif

namespace hyp
{
    using MeshShapeDataTable = TRangeTable<uint32_t>;

    class RenderBackend
    {
    public:
        RenderBackend(const Window& window, rhi::Backend rhiBackend, const std::filesystem::path& shaderDir, const std::filesystem::path& resourceDir);

        ~RenderBackend();

        void wakeUpRenderThread() noexcept;

        void shutdown();

        void drainPendingWork();

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

        PrepareQueue& getPrepareQueue() noexcept
        {
            return mPrepareQueue;
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

        TextureClearer& getTextureClearer() noexcept
        {
            return mTextureClearer;
        }

        // 稀疏体素数据(目前只有 Mesh 的距离场 brick)跨网格共享的物理存储池——见
        // DistanceFieldBrickAtlas 自己的注释。挂在 RenderBackend 而不是 Renderer 那层的
        // RendererContext 上, 是因为 MeshShapeProxy 这个消费方本身就在 RenderBase 里,
        // RenderBase 不能反过来依赖构建在它之上的 Renderer 模块
        DistanceFieldBrickAtlas& getMeshDistanceFieldBrickAtlas() noexcept
        {
            return mMeshDistanceFieldBrickAtlas;
        }

        // 跨网格共享的 Mesh 资产级别数据表(SDF metadata + 间接索引表 + Card 数据)——见
        // 每个网格在里面占一段连续区间, 字段布局见 MeshShapeProxy.h。归属层级跟 mMeshDistanceFieldBrickAtlas 同一个理由
        MeshShapeDataTable& getMeshShapeDataTable() noexcept
        {
            return mMeshShapeDataTable;
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
        PrepareQueue mPrepareQueue;
        DeferredTaskQueue mDeferredTaskQueue;
        ProxyManager mProxyManager;
        BindlessRegistry mBindlessRegistry;
        GlobalBindings mGlobalBindings;
        TextureClearer mTextureClearer;
        StagingAllocator mStagingAllocator;
        DistanceFieldBrickAtlas mMeshDistanceFieldBrickAtlas;
        MeshShapeDataTable mMeshShapeDataTable;
        RenderTagRegistry mRenderTagRegistry;
        MaterialShaderManager mMaterialShaderManager;

        FrameContext mFrameContext;
        bool mShutdown = false;
    };
}
