#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace hyp
{
    class DeferredTaskQueue
    {
    public:
        using Task = std::function<void()>;

        DeferredTaskQueue() = default;

        // 不能 =default——销毁顺序不保证这个队列一定是最后一个被销毁的成员, 晚于它析构的
        // 兄弟成员(比如 RenderBackend::mMeshDistanceFieldBrickAtlas, 声明在它后面)如果在自己析构
        // 时又往队列里塞了新任务, 那时候 RenderBackend::drainPendingWork() 里那次显式
        // flush() 早就跑完了, 不会再有人替这个新任务兜底——直接让容器隐式析构会把任务的
        // lambda 直接销毁而不是调用它, 里面捕获的 TRef<GPU 资源> 走的是自己的析构路径,
        // 绕开了"先 waitIdle 再销毁"这层保护。全引擎只有 RenderBackend::mDeferredTaskQueue
        // 这一份实例, 销毁前必然已经 waitIdle() 过, 这里再跑一次安全
        ~DeferredTaskQueue()
        {
            flush();
        }

        // deferredTicks 为 0 时表示在本帧结尾处执行
        void push(Task callback, uint32_t deferredTicks);

        void tick();

        void flush();

    private:
        struct TaskWrapper
        {
            Task callback;
            uint32_t deferredTicks;
        };
        std::vector<TaskWrapper> mTasks;
    };
}
