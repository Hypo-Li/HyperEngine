#pragma once
#include <vector>
#include <atomic>

namespace hyp
{
    // SPSC 安全的固定大小环形队列
    template <typename T>
    class alignas(64) TSPSCQueue
    {
    public:
        explicit TSPSCQueue(size_t capacity)
            : mCapacity(capacity + 1),
            mBuffer(static_cast<std::byte*>(::operator new[](sizeof(T) * mCapacity, std::align_val_t{alignof(T)}))),
            mHead(0), mTail(0)
        {
            static_assert(std::is_move_constructible<T>::value || std::is_copy_constructible<T>::value,
                      "T must be copyable or movable");
        }

        // 析构前需要保证无线程访问该对象
        ~TSPSCQueue() noexcept
        {
            size_t head = mHead.load(std::memory_order_relaxed);
            size_t tail = mTail.load(std::memory_order_relaxed);
            while (tail != head)
            {
                std::destroy_at(getElementPtr(tail));
                tail = increment(tail);
            }
            ::operator delete[](mBuffer, std::align_val_t{ alignof(T) });
        }

        TSPSCQueue(const TSPSCQueue&) = delete;
        TSPSCQueue& operator=(const TSPSCQueue&) = delete;

        // only for producer
        bool push(T&& item)
        {
            size_t head = mHead.load(std::memory_order_relaxed);
            size_t nextHead = increment(head);
            if (nextHead == mTail.load(std::memory_order_acquire))
                return false; // full

            std::construct_at(getElementPtr(head), std::move(item));
            mHead.store(nextHead, std::memory_order_release);
            return true;
        }

        bool push(const T& item)
        {
            size_t head = mHead.load(std::memory_order_relaxed);
            size_t nextHead = increment(head);
            if (nextHead == mTail.load(std::memory_order_acquire))
                return false; // full

            std::construct_at(getElementPtr(head), item);
            mHead.store(nextHead, std::memory_order_release);
            return true;
        }

        // only for consumer
        bool pop(T& outItem)
        {
            size_t tail = mTail.load(std::memory_order_relaxed);
            if (tail == mHead.load(std::memory_order_acquire))
                return false; // empty

            T* elem = getElementPtr(tail);
            outItem = std::move(*elem);
            std::destroy_at(elem);
            mTail.store(increment(tail), std::memory_order_release);
            return true;
        }

        // only for producer
        bool full() const noexcept
        {
            return increment(mHead.load(std::memory_order_relaxed)) == mTail.load(std::memory_order_acquire);
        }

        // only for consumer
        bool empty() const noexcept
        {
            return mTail.load(std::memory_order_relaxed) == mHead.load(std::memory_order_acquire);
        }

        size_t size() const noexcept
        {
            size_t head = mHead.load(std::memory_order_relaxed);
            size_t tail = mTail.load(std::memory_order_relaxed);
            if (head >= tail)
                return head - tail;
            return mCapacity - tail + head;
        }

        size_t capacity() const noexcept
        {
            return mCapacity - 1;
        }

    private:
        size_t increment(size_t index) const noexcept
        {
            return (index + 1) % mCapacity;
        }

        T* getElementPtr(size_t index) const noexcept
        {
            return std::launder(reinterpret_cast<T*>(&mBuffer[index * sizeof(T)]));
        }

    private:
        // 实际容量大小, 包括哨兵位
        size_t mCapacity;
        std::byte* mBuffer;
        // 使用 alignas(64) 避免mHead, mTail在同一cache line, 减少false sharing
        alignas(64) std::atomic_size_t mHead;
        alignas(64) std::atomic_size_t mTail;
    };
}
