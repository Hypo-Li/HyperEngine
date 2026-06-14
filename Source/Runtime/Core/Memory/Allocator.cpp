#include "Allocator.h"
#include "../Common/LogSystem.h"

namespace hyp
{
    LinearAllocator::LinearAllocator(void* begin, void* end) noexcept
        : mBegin(begin), mCapacity(static_cast<uint32_t>(uintptr_t(end) - uintptr_t(begin)))
    {
    }

    LinearAllocator::LinearAllocator(LinearAllocator&& other) noexcept
    {
        this->swap(other);
    }

    LinearAllocator& LinearAllocator::operator=(LinearAllocator&& other) noexcept
    {
        if (this != &other)
        {
            this->swap(other);
        }
        return *this;
    }

    void* LinearAllocator::allocate(size_t size, size_t alignment, size_t extra) noexcept
    {
        void* const p = pointerAlign(current(), alignment, extra);
        void* const c = pointerAdd(p, size);
        bool success = c <= end();
        assert(success);
        setCurrent(success ? c : current());
        return success ? p : nullptr;
    }

    void LinearAllocator::swap(LinearAllocator& other) noexcept
    {
        std::swap(mBegin, other.mBegin);
        std::swap(mCapacity, other.mCapacity);
        std::swap(mCurrent, other.mCurrent);
    }

    Arena::Arena(size_t size)
        : mArea(size),
        mAllocator(mArea)
    {

    }

    Arena::~Arena() noexcept
    {
    }

}
