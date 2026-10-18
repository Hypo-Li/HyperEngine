#pragma once
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <cassert>

namespace hyp
{
    template <typename P, typename T>
    static inline P* pointerAdd(P* p, T t) noexcept
    {
        return (P*)(uintptr_t(p) + uintptr_t(t));
    }

    template <typename P>
    static inline P* pointerAlign(P* p, size_t alignment) noexcept
    {
        // alignment must be a power-of-two
        assert(alignment && !(alignment & alignment - 1));
        return (P*)((uintptr_t(p) + alignment - 1) & ~(alignment - 1));
    }

    template <typename P>
    static inline P* pointerAlign(P* p, size_t alignment, size_t offset) noexcept
    {
        P* const r = pointerAlign(pointerAdd(p, offset), alignment);
        assert(r >= pointerAdd(p, offset));
        return r;
    }

    /* ------------------------------------------------------------------------------------------------
     * LinearAllocator
     *
     * + Allocates blocks linearly
     * + Cannot free individual blocks
     * + Can free top of memory back up to a specified point
     * + Doesn't call destructors
     * ------------------------------------------------------------------------------------------------
     */

    class LinearAllocator
    {
    public:
        LinearAllocator(void* begin, void* end) noexcept;

        template <typename AREA>
        explicit LinearAllocator(const AREA& area) : LinearAllocator(area.begin(), area.end()) { }

        LinearAllocator(const LinearAllocator& other) = delete;
        LinearAllocator& operator=(const LinearAllocator& other) = delete;

        LinearAllocator(LinearAllocator&& other) noexcept;
        LinearAllocator& operator=(LinearAllocator&& other) noexcept;

        ~LinearAllocator() noexcept = default;

        void* allocate(size_t size, size_t alignment = alignof(std::max_align_t), size_t extra = 0) noexcept;

        void free(void*, size_t) noexcept { }

        void reset() noexcept
        {
            setCurrent(mBegin);
        }

        void swap(LinearAllocator& other) noexcept;

        void* getCurrent() noexcept
        {
            return current();
        }

        uint32_t size() const noexcept
        {
            return mCurrent;
        }

    private:
        void* end() noexcept
        {
            return pointerAdd(mBegin, mCapacity);
        }

        void* current() noexcept
        {
            return pointerAdd(mBegin, mCurrent);
        }

        void setCurrent(void* p) noexcept
        {
            mCurrent = uint32_t(uintptr_t(p) - uintptr_t(mBegin));
        }

    private:
        void* mBegin = nullptr;
        uint32_t mCapacity = 0;
        uint32_t mCurrent = 0;
    };

    class HeapArea
    {
    public:
        HeapArea() noexcept = default;

        explicit HeapArea(size_t size)
        {
            if (size)
            {
                mBegin = std::malloc(size);
                mEnd = pointerAdd(mBegin, size);
            }
        }

        ~HeapArea() noexcept
        {
            std::free(mBegin);
        }

        HeapArea(const HeapArea& rhs) = delete;
        HeapArea& operator=(const HeapArea& rhs) = delete;
        HeapArea(HeapArea&& rhs) noexcept = delete;
        HeapArea& operator=(HeapArea&& rhs) noexcept = delete;

        void* data() const noexcept { return mBegin; }
        void* begin() const noexcept { return mBegin; }
        void* end() const noexcept { return mEnd; }
        size_t size() const noexcept { return uintptr_t(mEnd) - uintptr_t(mBegin); }

        friend void swap(HeapArea& lhs, HeapArea& rhs) noexcept
        {
            std::swap(lhs.mBegin, rhs.mBegin);
            std::swap(lhs.mEnd, rhs.mEnd);
        }

    private:
        void* mBegin = nullptr;
        void* mEnd = nullptr;
    };

    class Arena
    {
    public:
        Arena(size_t size);

        ~Arena() noexcept;

        void* allocate(size_t size, size_t alignment = alignof(std::max_align_t), size_t extra = 0) noexcept
        {
            void* p = mAllocator.allocate(size, alignment, extra);
            return p;
        }

        template <typename T> requires std::is_trivially_destructible_v<T>
        T* allocate(size_t count, size_t alignment = alignof(T), size_t extra = 0) noexcept
        {
            return (T*)allocate(count * sizeof(T), alignment, extra);
        }

        void free(void* p, size_t size) noexcept
        {
            if (p)
            {
                mAllocator.free(p, size);
            }
        }

        void reset() noexcept
        {
            mAllocator.reset();
        }

        template <typename T, typename... Args>
        T* create(Args&& ... args) noexcept
        {
            void* const p = this->allocate(sizeof(T), alignof(T));
            return p ? new(p) T(std::forward<Args>(args)...) : nullptr;
        }

        template <typename T>
        void destroy(T* p) noexcept
        {
            static_assert(std::is_final_v<T>, "T must be final");
            if (p)
            {
                std::destroy_at(p);
                this->free((void*)p, sizeof(T));
            }
        }

        const LinearAllocator& getAllocator() const noexcept
        {
            return mAllocator;
        }

    private:
        HeapArea mArea;
        LinearAllocator mAllocator;
    };

    template <typename T>
    class StlAllocator
    {
        template <typename U>
        friend class StlAllocator;
    public:
        using value_type = T;

    public:
        StlAllocator(Arena& arena) : mArena(arena) {}

        template <typename U>
        explicit StlAllocator(const StlAllocator<U>& other) : mArena(other.mArena) {}

        template <typename U>
        bool operator==(const StlAllocator<U>& other) const noexcept
        {
            return std::addressof(mArena) == std::addressof(other.mArena);
        }

        template <typename U>
        bool operator!=(const StlAllocator<U>& other) const noexcept
        {
            return !operator==(other);
        }

        T* allocate(size_t n)
        {
            T* p = static_cast<T*>(mArena.allocate(n * sizeof(T), alignof(T)));
            assert(p && "Arena out of memory");
            return p;
        }

        void deallocate(T* p, size_t n)
        {
            mArena.free(p, n * sizeof(T));
        }

    private:
        Arena& mArena;
    };

    template <typename T>
    using ArenaVector = std::vector<T, StlAllocator<T>>;
}
