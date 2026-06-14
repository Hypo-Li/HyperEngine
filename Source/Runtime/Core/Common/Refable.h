#pragma once
#include <atomic>

namespace hyp
{
    // NOTE: 我们需要考虑是否使用Refable作为Object类型的基类, 后者完全可以使用标志清除法做GC

    struct RefableAccessor;

    class Refable
    {
        friend struct RefableAccessor;
        template <typename T>
        friend class TRef;
    public:
        size_t getRefCount() const noexcept
        {
            return mRefCount.load(std::memory_order_relaxed);
        }

    protected:
        Refable() noexcept : mRefCount(0) {}
        virtual ~Refable() noexcept = default;

    private:
        mutable std::atomic_size_t mRefCount;

        size_t incRef() const noexcept
        {
            return mRefCount.fetch_add(1, std::memory_order_relaxed) + 1;
        }

        size_t decRef() noexcept
        {
            size_t old = mRefCount.fetch_sub(1, std::memory_order_release);
            if (old == 1)
            {
                std::atomic_thread_fence(std::memory_order_acquire);
                return 0;
            }
            return old - 1;
        }
    };

    struct RefableAccessor
    {
        static void incRef(Refable& refable)
        {
            refable.incRef();
        }

        static void decRef(Refable& refable)
        {
            refable.decRef();
        }
    };

    class Object;
    void destroyObject(Object* object) noexcept;

    template <typename T>
    class TRef
    {
    public:
        TRef() noexcept : mPtr(nullptr) {}

        TRef(T* ptr) noexcept : mPtr(ptr) { if (ptr) mPtr->incRef(); }

        TRef(const TRef& other) noexcept : mPtr(other.mPtr) { if (mPtr) mPtr->incRef(); }

        TRef(TRef&& other) noexcept : mPtr(other.mPtr)
        {
            other.mPtr = nullptr;
        }

        template <typename U> requires std::convertible_to<U*, T*>
        TRef(const TRef<U>& other) noexcept : mPtr(other.get())
        {
            if (mPtr)
                mPtr->incRef();
        }

        ~TRef() noexcept { reset(); }

        T* get() const noexcept
        {
            return mPtr;
        }

        void reset(T* ptr = nullptr) noexcept
        {
            if (mPtr == ptr)
                return;

            T* old = mPtr;
            mPtr = ptr;
            if (mPtr)
                mPtr->incRef();
            if (old && old->decRef() == 0)
                destroy(old);
        }

        TRef& operator=(const TRef& ref) noexcept
        {
            if (mPtr != ref.mPtr)
            {
                reset(ref.mPtr);
            }
            return *this;
        }

        TRef& operator=(T* ptr) noexcept
        {
            if (mPtr != ptr)
            {
                reset(ptr);
            }
            return *this;
        }

        TRef& operator=(TRef&& ref) noexcept
        {
            if (this != &ref)
            {
                std::swap(mPtr, ref.mPtr);
            }
            return *this;
        }

        template <typename U> requires std::convertible_to<U*, T*>
        TRef& operator=(const TRef<U>& ref) noexcept
        {
            if (mPtr != ref.get())
            {
                reset(ref.get());
            }
            return *this;
        }

        operator T* () const noexcept
        {
            return mPtr;
        }

        T* operator->() noexcept
        {
            return mPtr;
        }

        const T* operator->() const noexcept
        {
            return mPtr;
        }

        bool operator==(const TRef& ref) const noexcept
        {
            return mPtr == ref.mPtr;
        }

        bool operator==(T* ptr) const noexcept
        {
            return mPtr == ptr;
        }

        operator bool() const noexcept
        {
            return mPtr != nullptr;
        }

        void swap(TRef& other) noexcept
        {
            std::swap(mPtr, other.mPtr);
        }

        friend void swap(TRef& a, TRef& b) noexcept
        {
            a.swap(b);
        }

    private:
        static void destroy(T* ptr) noexcept
        {
            if constexpr (std::is_base_of_v<Object, T>)
            {
                destroyObject(ptr);
            }
            else
            {
                delete ptr;
            }
        }

    private:
        T* mPtr;
    };
}

namespace std
{
    template <typename T>
    struct hash<hyp::TRef<T>>
    {
        std::size_t operator()(const hyp::TRef<T>& ref) const noexcept
        {
            return std::hash<T*>()(ref.get());
        }
    };
}
