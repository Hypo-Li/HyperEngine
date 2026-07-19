#pragma once
#ifndef HYP_SINGLE_THREADED_RENDERING
#include <semaphore>
#endif

#include <chrono>
#include <future>
#include <functional>
#include <memory>
#include <vector>

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
                waitForCommandQueueSpace();
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

        template <typename T, typename Callback>
        void onFutureReady(std::future<T>&& future, Callback&& callback)
        {
            auto sharedFuture = std::make_shared<std::future<T>>(std::move(future));
            mPendingFutures.emplace_back(
                [sharedFuture,
                callback = std::forward<Callback>(callback)]() mutable
                {
                    if (sharedFuture->wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                        return false;

                    try
                    {
                        callback(sharedFuture->get());
                    }
                    catch (const std::future_error&)
                    {
                        // A render-side producer can disappear while an object is being
                        // unregistered. A broken promise simply cancels that result.
                    }
                    return true;
                }
            );
        }

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
        void waitForCommandQueueSpace();
        void pollFutures();
        RenderCommandQueue& getCommandQueue() noexcept;

    private:
        RenderBackend& mBackend;
#ifndef HYP_SINGLE_THREADED_RENDERING
        static constexpr uint32_t kMaxQueuedFrames = 2;
        std::counting_semaphore<kMaxQueuedFrames> mFrameSemaphore{ kMaxQueuedFrames };
#endif
        ProxyHandleAllocator mProxyHandleAllocator;
        std::vector<std::function<bool()>> mPendingFutures;
    };
}
