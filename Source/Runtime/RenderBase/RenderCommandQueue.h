#pragma once
#include "FrameContext.h"

namespace hyp
{
    class IRenderCommand
    {
    public:
        IRenderCommand() noexcept = default;

        virtual ~IRenderCommand() noexcept = default;

        virtual void execute(FrameContext& context) = 0;
    };

    template <typename LambdaType>
    class TRenderCommand : public IRenderCommand
    {
    public:
        TRenderCommand(LambdaType&& lambda) noexcept
            : mLambda(std::move(lambda))
        {
        }

        TRenderCommand(TRenderCommand&& other) noexcept
            : mLambda(std::move(other.mLambda))
        {
        }

        TRenderCommand(const TRenderCommand&) = delete;
        TRenderCommand& operator=(const TRenderCommand&) = delete;

        virtual void execute(FrameContext& context) override
        {
            mLambda(context);
        }

    private:
        std::decay_t<LambdaType> mLambda;
    };

    static constexpr size_t kCacheLineSize = std::hardware_destructive_interference_size;

    // SPSC Render Command Queue
    class alignas(kCacheLineSize) RenderCommandQueue
    {
        struct CommandHeader
        {
            uint32_t isWrap;
            uint32_t size;
        };

        static consteval size_t alignSize(size_t size, size_t alignment)
        {
            return (size + alignment - 1) & ~(alignment - 1);
        }

        static constexpr size_t kMaxAlignment = alignof(std::max_align_t);
    public:
        static constexpr size_t kHeaderSize = (sizeof(CommandHeader) + kMaxAlignment - 1) & ~(kMaxAlignment - 1);
        explicit RenderCommandQueue(size_t bufferSize);

        RenderCommandQueue(const RenderCommandQueue&) = delete;
        RenderCommandQueue& operator=(const RenderCommandQueue&) = delete;

        ~RenderCommandQueue() noexcept;

        // 生产者检查空间是否能够容纳某个命令
        template <typename T>
        bool hasSpaceFor() const noexcept
        {
            using CommandType = std::decay_t<T>;
            static constexpr size_t kCmdSize = alignSize(sizeof(CommandType), kMaxAlignment);
            static constexpr size_t kTotalSize = kHeaderSize + kCmdSize;

            size_t head = mHead.load(std::memory_order_relaxed);
            size_t tail = mTail.load(std::memory_order_acquire);

            if (head >= tail)
            {
                if (head + kTotalSize >= mCapacity)
                {
                    // 考虑绕回后的空间
                    return kTotalSize < tail;
                }
            }
            else
            {
                return (head + kTotalSize < tail);
            }
            return true;
        }

        /**
         * 生产者：压入一个命令对象
         */
        template <typename T>
        bool enqueueCommand(T&& cmd)
        {
            using CommandType = std::decay_t<T>;
            static_assert(std::is_base_of_v<IRenderCommand, std::decay_t<T>>, "T must derive from IRenderCommand");
            static_assert(alignof(CommandType) <= kMaxAlignment, "T must be aligned to kMaxAlignment");

            static constexpr size_t kCmdSize = alignSize(sizeof(CommandType), kMaxAlignment);
            static constexpr size_t kTotalSize = kHeaderSize + kCmdSize;

            size_t head = mHead.load(std::memory_order_relaxed);
            size_t tail = mTail.load(std::memory_order_acquire);

            if (head >= tail)
            {
                // [empty { tail cmds head } empty]
                // 检查缓冲区末尾是否有足够空间
                if (head + kTotalSize >= mCapacity)
                {
                    // 末尾放不下，检查头部是否有空间
                    // head == tail应当表示全空, 因此即便当 kTotalSize == tail 时, 也不能容纳该指令
                    if (kTotalSize >= tail)
                        return false; // 头部也空间不足, 放弃

                    // 头部能够放下, 在末尾放置 Wrap 标记, 表明数据已绕回
                    CommandHeader* wrapHdr = reinterpret_cast<CommandHeader*>(&mBuffer[head]);
                    wrapHdr->isWrap = 1;
                    wrapHdr->size = static_cast<uint32_t>(mCapacity - head);

                    // 更新 Head 到 0，准备从头开始写
                    head = 0;
                }
            }
            else
            {
                // [ cmds head } empty { tail cmds ]
                // head 已经在 tail 后面，直接检查是否追上
                if (head + kTotalSize >= tail)
                    return false; // 缓冲区已满
            }

            // 写入数据
            CommandHeader *hdr = reinterpret_cast<CommandHeader*>(&mBuffer[head]);
            hdr->size = static_cast<uint32_t>(kTotalSize);
            hdr->isWrap = 0;

            void* cmdPos = &mBuffer[head + kHeaderSize];
            std::construct_at(reinterpret_cast<CommandType*>(cmdPos), std::forward<T>(cmd));

            mHead.store(head + kTotalSize, std::memory_order_release);
            return true;
        }

        /*
        *  消费者：执行所有挂起的命令
        */
        void executeAll(FrameContext& context);

        bool empty() const noexcept;

    private:
        const size_t mCapacity;
        std::byte* const mBuffer;

        // Head 和 Tail 分别独占一个缓存行, 避免伪共享
        alignas(kCacheLineSize) std::atomic_size_t mHead;
        alignas(kCacheLineSize) std::atomic_size_t mTail;
    };
}
