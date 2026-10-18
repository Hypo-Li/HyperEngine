#include "RangeAllocator.h"

#include <algorithm>
#include <cassert>

namespace hyp
{
    RangeAllocator::RangeAllocator(uint32_t capacity)
        : mCapacity(capacity)
    {
        if (capacity > 0)
        {
            mFreeRanges.push_back({ 0, capacity });
        }
    }

    std::optional<uint32_t> RangeAllocator::allocate(uint32_t count) noexcept
    {
        if (count == 0)
            return 0u;

        for (size_t i = 0; i < mFreeRanges.size(); ++i)
        {
            auto& [offset, size] = mFreeRanges[i];
            if (size < count)
                continue;

            const uint32_t result = offset;
            if (size == count)
                mFreeRanges.erase(mFreeRanges.begin() + i);
            else
            {
                offset += count;
                size -= count;
            }
            return result;
        }
        return std::nullopt;
    }

    void RangeAllocator::free(uint32_t offset, uint32_t count) noexcept
    {
        if (count == 0)
            return;

        assert(offset + count <= mCapacity && "归还的区间超出了这个分配器的容量, 调用方传的 offset/count 跟当初 allocate() 返回的对不上");

        auto it = std::lower_bound(
            mFreeRanges.begin(), mFreeRanges.end(), offset,
            [](const std::pair<uint32_t, uint32_t>& range, uint32_t value) { return range.first < value; }
        );

        // 归还的区间不应该跟任何现存空闲区间重叠——重叠说明调用方重复 free 了同一段,
        // 或者传的 offset/count 本身就是错的
        assert((it == mFreeRanges.end() || offset + count <= it->first) && "归还的区间跟后一个空闲区间重叠, 疑似重复 free 或 offset/count 有误");
        assert((it == mFreeRanges.begin() || (it - 1)->first + (it - 1)->second <= offset) && "归还的区间跟前一个空闲区间重叠, 疑似重复 free 或 offset/count 有误");

        it = mFreeRanges.insert(it, { offset, count });

        if (it + 1 != mFreeRanges.end() && it->first + it->second == (it + 1)->first)
        {
            it->second += (it + 1)->second;
            mFreeRanges.erase(it + 1);
        }

        if (it != mFreeRanges.begin())
        {
            auto prev = it - 1;
            if (prev->first + prev->second == it->first)
            {
                prev->second += it->second;
                mFreeRanges.erase(it);
            }
        }
    }
}
