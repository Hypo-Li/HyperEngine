#include "RenderCommandQueue.h"

namespace hyp
{
    RenderCommandQueue::RenderCommandQueue(size_t bufferSize) :
        mCapacity(bufferSize),
        // 为了在尾部容纳WrapCommandHeader, 我们需要多分配 kHeaderSize 的空间
        mBuffer(static_cast<std::byte*>(::operator new[](bufferSize + kHeaderSize, std::align_val_t{ kMaxAlignment }))),
        mHead(0), mTail(0)
    {
    }

    RenderCommandQueue::~RenderCommandQueue() noexcept
    {
        // 假设：析构前已确保没有生产者/消费者在访问此对象
        size_t tail = mTail.load(std::memory_order_relaxed);
        const size_t head = mHead.load(std::memory_order_relaxed);

        // 遍历缓冲区中所有尚未执行/清理的命令
        while (tail != head)
        {
            CommandHeader* hdr = reinterpret_cast<CommandHeader*>(&mBuffer[tail]);

            if (hdr->isWrap)
            {
                // 碰到绕回标记，直接跳回头部继续清理
                tail = 0;
                // 如果 head 正好在 0，说明后面没有数据了，直接退出
                if (tail == head)
                    break;
                hdr = reinterpret_cast<CommandHeader*>(&mBuffer[tail]);
            }

            // 定位到命令对象
            void* cmdPos = &mBuffer[tail + kHeaderSize];
            IRenderCommand* renderCmd = reinterpret_cast<IRenderCommand*>(cmdPos);

            // 关键：调用虚析构函数。
            std::destroy_at(renderCmd);

            // 移动到下一个位置
            tail += hdr->size;
        }

        ::operator delete[](mBuffer, std::align_val_t{ kMaxAlignment });
    }

    void RenderCommandQueue::executeAll(RenderFrameContext& context)
    {
        size_t tail = mTail.load(std::memory_order_relaxed);

        while (true)
        {
            const size_t head = mHead.load(std::memory_order_acquire);

            if (tail == head)
                break;

            while (tail != head)
            {
                CommandHeader* hdr = reinterpret_cast<CommandHeader*>(&mBuffer[tail]);

                if (hdr->isWrap)
                {
                    // 跳转到头部
                    tail = 0;
                    // 立刻更新 tail 到 0，通知生产者末尾空间已释放
                    mTail.store(tail, std::memory_order_release);
                    if (tail == head)
                        break;
                    hdr = reinterpret_cast<CommandHeader*>(&mBuffer[tail]);
                }

                void* cmdPos = &mBuffer[tail + kHeaderSize];
                IRenderCommand* renderCmd = reinterpret_cast<IRenderCommand*>(cmdPos);
                renderCmd->execute(context);
                std::destroy_at(renderCmd);

                tail += hdr->size;

                mTail.store(tail, std::memory_order_release);
            }
        }
    }

    bool RenderCommandQueue::empty() const noexcept
    {
        return mTail.load(std::memory_order_acquire) == mHead.load(std::memory_order_acquire);
    }

}
