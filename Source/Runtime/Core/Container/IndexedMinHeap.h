#pragma once
#include <cassert>
#include <cstdint>
#include <vector>

namespace hyp
{
    // 元素是 [0, idCapacity) 内的整数 id、键是 uint32 的二叉最小堆, 按 id 记录堆内位置,
    // 所以 insert/update/remove 都是 O(log n)
    class IndexedMinHeap
    {
    public:
        explicit IndexedMinHeap(uint32_t idCapacity) :
            mKeys(idCapacity, 0u),
            mPositions(idCapacity, kInvalidPosition)
        {
            mHeap.reserve(idCapacity);
        }

        bool empty() const noexcept
        {
            return mHeap.empty();
        }

        bool contains(uint32_t id) const noexcept
        {
            return mPositions[id] != kInvalidPosition;
        }

        uint32_t top() const noexcept
        {
            assert(!mHeap.empty());
            return mHeap.front();
        }

        uint32_t getKey(uint32_t id) const noexcept
        {
            assert(contains(id));
            return mKeys[id];
        }

        void insert(uint32_t id, uint32_t key) noexcept
        {
            assert(!contains(id));
            mKeys[id] = key;
            mPositions[id] = static_cast<uint32_t>(mHeap.size());
            mHeap.push_back(id);
            siftUp(mPositions[id]);
        }

        void update(uint32_t id, uint32_t key) noexcept
        {
            assert(contains(id));
            const uint32_t oldKey = mKeys[id];
            mKeys[id] = key;
            if (key < oldKey)
                siftUp(mPositions[id]);
            else
                siftDown(mPositions[id]);
        }

        void remove(uint32_t id) noexcept
        {
            assert(contains(id));
            const uint32_t position = mPositions[id];
            const uint32_t lastId = mHeap.back();
            mHeap.pop_back();
            mPositions[id] = kInvalidPosition;
            if (lastId == id)
                return;

            mHeap[position] = lastId;
            mPositions[lastId] = position;
            siftUp(position);
            siftDown(mPositions[lastId]);
        }

    private:
        static constexpr uint32_t kInvalidPosition = 0xFFFFFFFFu;

        void swapEntries(uint32_t a, uint32_t b) noexcept
        {
            const uint32_t idA = mHeap[a];
            const uint32_t idB = mHeap[b];
            mHeap[a] = idB;
            mHeap[b] = idA;
            mPositions[idB] = a;
            mPositions[idA] = b;
        }

        void siftUp(uint32_t position) noexcept
        {
            while (position > 0)
            {
                const uint32_t parent = (position - 1) / 2;
                if (mKeys[mHeap[position]] >= mKeys[mHeap[parent]])
                    break;
                swapEntries(position, parent);
                position = parent;
            }
        }

        void siftDown(uint32_t position) noexcept
        {
            const uint32_t count = static_cast<uint32_t>(mHeap.size());
            while (true)
            {
                const uint32_t left = position * 2 + 1;
                const uint32_t right = left + 1;
                uint32_t smallest = position;
                if (left < count && mKeys[mHeap[left]] < mKeys[mHeap[smallest]])
                    smallest = left;
                if (right < count && mKeys[mHeap[right]] < mKeys[mHeap[smallest]])
                    smallest = right;
                if (smallest == position)
                    break;
                swapEntries(position, smallest);
                position = smallest;
            }
        }

        std::vector<uint32_t> mHeap;
        std::vector<uint32_t> mKeys;
        std::vector<uint32_t> mPositions;
    };
}
