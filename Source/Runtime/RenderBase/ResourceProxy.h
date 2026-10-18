#pragma once
#include "RenderProxy.h"

namespace hyp
{
    namespace rhi
    {
        class CommandList;
    }

    class PrepareQueue;

    // 不属于任何场景、被场景元素按 handle 共享的资源侧代理, 与 SceneElementProxy 对称
    class ResourceProxy : public RenderProxy
    {
        friend class PrepareQueue;
    public:
        ResourceProxy() = default;

        ~ResourceProxy() noexcept override;

        // 返回 false 表示还没做完, 继续留在 PrepareQueue 里等下一帧
        virtual bool prepare(rhi::CommandList& cmdList)
        {
            return true;
        }

        void markNeedsPrepare();

    private:
        static constexpr uint32_t kNotQueued = ~0u;

        uint32_t mPrepareQueueIndex = kNotQueued;
    };
}
