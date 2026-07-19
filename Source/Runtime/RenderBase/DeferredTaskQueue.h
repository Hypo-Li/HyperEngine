#pragma once
#include <functional>
#include <vector>

namespace hyp
{
    class DeferredTaskQueue
    {
    public:
        using Task = std::function<void()>;

        DeferredTaskQueue() = default;

        ~DeferredTaskQueue() = default;

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
