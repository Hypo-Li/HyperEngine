#include "DeferredTaskQueue.h"
#include <iterator>

namespace hyp
{
    void DeferredTaskQueue::push(Task callback, uint32_t deferredTicks)
    {
        mTasks.emplace_back(std::move(callback), deferredTicks);
    }

    void DeferredTaskQueue::tick()
    {
        // 1. 找出所有到期的任务
        auto it = std::stable_partition(mTasks.begin(), mTasks.end(), [](const TaskWrapper& task) {
            return task.deferredTicks != 0; // 还没到期的放前面
        });

        // 2. 将到期的拷贝/移动出来
        std::vector<TaskWrapper> readyTasks;
        std::move(it, mTasks.end(), std::back_inserter(readyTasks));
        mTasks.erase(it, mTasks.end());

        // 3. 递减剩下的任务
        for (auto& task : mTasks)
        {
            task.deferredTicks--;
        }

        // 4. 最后执行回调（避免在操作容器时触发可能修改容器的回调）
        for (auto& task : readyTasks)
        {
            task.callback();
        }
    }

    void DeferredTaskQueue::flush()
    {
        // 使用 while 循环，因为执行任务过程中可能会产生新的任务（递归销毁）
        while (!mTasks.empty())
        {
            // 1. 将当前所有任务移动到一个局部容器，并清空原容器
            // 这样在执行 callback 时，如果里面又有 push，新任务会安全地进入空的 mTasks
            std::vector<TaskWrapper> tasksToFlush = std::move(mTasks);
            mTasks.clear(); // 确保原容器彻底干净

            // 2. 执行这一批次的所有任务
            for (auto& task : tasksToFlush)
            {
                if (task.callback)
                {
                    task.callback();
                }
            }

            // 3. 循环会继续，直到某次执行后没有新的任务被 push 进来
        }
    }

}
