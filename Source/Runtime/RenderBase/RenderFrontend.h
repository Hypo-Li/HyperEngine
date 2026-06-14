#pragma once
#include <semaphore>

#include "Vulkan/Image.h"
#include "ProxyHandle.h"
#include "RenderCommandQueue.h"
#include "Shader.h"

namespace hyp
{
    class RenderBackend;
    class RenderFrontend
    {
    public:
        RenderFrontend(RenderBackend& backend);

        template <typename T>
        void enqueueCommand(T&& command)
        {
            auto& commandQueue = getCommandQueue();
            while (!commandQueue.hasSpaceFor<T>())
            {
                wakeUpRenderThread();
                std::this_thread::yield();
            }
            commandQueue.enqueueCommand(std::forward<T>(command));
        }

        template <typename T>
        void enqueueLambda(T&& lambda)
        {
            enqueueCommand(TRenderCommand<T>(std::forward<T>(lambda)));
        }

        void wakeUpRenderThread();

        void beginFrame();

        void endFrame();

        void render(double time, float deltaTime);

        template <ProxyType PROXY_TYPE>
        TProxyHandle<PROXY_TYPE> allocateProxyHandle() noexcept
        {
            return mProxyHandleAllocator.allocate<PROXY_TYPE>();
        }

        template <ProxyType PROXY_TYPE>
        void freeProxyHandle(TProxyHandle<PROXY_TYPE> handle) noexcept
        {
            mProxyHandleAllocator.free(handle);
        }

        MaterialShaderManager& getMaterialShaderManager() const noexcept;

    private:
        RenderCommandQueue& getCommandQueue() noexcept;

    private:
        RenderBackend& mBackend;
        // 渲染指令队列能够缓存的逻辑帧指令数
        // 2 代表：1帧正在渲染，1帧正在提交指令，逻辑线程最多超前1帧
        static constexpr uint32_t kMaxQueuedFrames = 2;
        std::counting_semaphore<kMaxQueuedFrames> mFrameSemaphore{ kMaxQueuedFrames };
        ProxyHandleAllocator mProxyHandleAllocator;
    };
}
