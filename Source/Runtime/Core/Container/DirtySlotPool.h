#pragma once
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace hyp
{
    template <typename T>
    class TDirtySlotPool
    {
    public:
        using UpdateRange = std::pair<size_t, size_t>;

        explicit TDirtySlotPool(size_t reserveCount)
        {
            mElements.reserve(reserveCount);
        }

        uint32_t acquire()
        {
            if (!mFreeIndices.empty())
            {
                uint32_t index = mFreeIndices.back();
                mFreeIndices.pop_back();
                return index;
            }

            uint32_t index = static_cast<uint32_t>(mElements.size());
            mElements.emplace_back();
            mUpdatedBitset.resize((mElements.size() + kBitsPerWord - 1) / kBitsPerWord, 0);
            return index;
        }

        void release(uint32_t index)
        {
            mFreeIndices.push_back(index);
        }

        T& operator[](uint32_t index) noexcept
        {
            assert(index < mElements.size());
            return mElements[index];
        }

        const T& operator[](uint32_t index) const noexcept
        {
            assert(index < mElements.size());
            return mElements[index];
        }

        T* data() noexcept
        {
            return mElements.data();
        }

        std::span<const T> getElements() const noexcept
        {
            return mElements;
        }

        uint32_t size() const noexcept
        {
            return static_cast<uint32_t>(mElements.size());
        }

        void markUpdated(uint32_t index) noexcept
        {
            assert((index / kBitsPerWord) < mUpdatedBitset.size());
            mUpdatedMinIndex = std::min(index, mUpdatedMinIndex);
            mUpdatedMaxIndex = std::max(index, mUpdatedMaxIndex);
            mUpdatedBitset[index / kBitsPerWord] |= 1ull << (index % kBitsPerWord);
        }

        std::vector<UpdateRange> generateUpdateRanges() const
        {
            std::vector<UpdateRange> ranges;
            if (mUpdatedBitset.empty())
                return ranges;

            static constexpr size_t kMaxHoleInElements = 64;
            uint32_t currentStart = UINT32_MAX;
            uint32_t lastDirtyIndex = 0;

            uint32_t minWord = mUpdatedMinIndex / kBitsPerWord;
            uint32_t maxWord = mUpdatedMaxIndex / kBitsPerWord;

            for (uint32_t w = minWord; w <= maxWord; ++w)
            {
                uint64_t word = mUpdatedBitset[w];
                if (word == 0)
                    continue;

                uint32_t firstInWord = w * kBitsPerWord + std::countr_zero(word);
                uint32_t lastInWord = w * kBitsPerWord + (kBitsPerWord - 1 - std::countl_zero(word));

                if (currentStart == UINT32_MAX)
                {
                    currentStart = firstInWord;
                }
                else if (firstInWord - lastDirtyIndex > kMaxHoleInElements)
                {
                    ranges.emplace_back(currentStart, lastDirtyIndex - currentStart + 1);
                    currentStart = firstInWord;
                }
                lastDirtyIndex = lastInWord;
            }

            if (currentStart != UINT32_MAX)
                ranges.emplace_back(currentStart, lastDirtyIndex - currentStart + 1);

            return ranges;
        }

        void clearUpdated() noexcept
        {
            std::fill(mUpdatedBitset.begin(), mUpdatedBitset.end(), 0);
            mUpdatedMinIndex = UINT32_MAX;
            mUpdatedMaxIndex = 0;
        }

    private:
        static constexpr uint32_t kBitsPerWord = 64;

        std::vector<T> mElements;
        std::vector<uint32_t> mFreeIndices;
        std::vector<uint64_t> mUpdatedBitset;
        uint32_t mUpdatedMinIndex = UINT32_MAX;
        uint32_t mUpdatedMaxIndex = 0;
    };
}
