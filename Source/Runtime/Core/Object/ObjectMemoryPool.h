#pragma once
#include <memory>
#include <cassert>
#include <cmath>

#include "ObjectDefines.h"
#include "../Common/Types.h"

namespace hyp
{
    template <ObjectType T, AllocateFrequency ALLOC_FREQ>
    class ObjectMemoryPool
    {
        // 每个 Chunk 中包含的对象数量
        static constexpr size_t kChunkCapacity = poolChunkCapacity<T, ALLOC_FREQ>();
    public:
        ObjectMemoryPool()
        {
            mVersions.reserve(kChunkCapacity);
            mFreeIndices.reserve(kChunkCapacity);
        }

        ~ObjectMemoryPool()
        {
            assert(mFreeIndices.size() == mVersions.size());
        }

        T* allocate(ObjectHandle& handle)
        {
            if (mFreeIndices.empty())
            {
                uint32_t indexOffset = static_cast<uint32_t>(mChunks.size() * kChunkCapacity);
                mChunks.emplace_back(std::make_unique<Chunk>());
                mVersions.resize(mVersions.size() + kChunkCapacity, 0);

                mFreeIndices.resize(kChunkCapacity);
                for (size_t i = 0; i < kChunkCapacity; ++i)
                {
                    mFreeIndices[i] = static_cast<uint32_t>(indexOffset + (kChunkCapacity - i - 1));
                }
            }
            uint32_t index = mFreeIndices.back();
            mFreeIndices.pop_back();
            handle.index = index;
            if (mVersions[index] == 0)
                ++mVersions[index];
            handle.version = mVersions[index];

            Chunk& chunk = *mChunks[index / kChunkCapacity];
            T* object = std::launder(reinterpret_cast<T*>(chunk.slots[index % kChunkCapacity].storage));
            return object;
        }

        void free(ObjectHandle handle)
        {
            assert(isValid(handle));

            mVersions[handle.index]++;
            mFreeIndices.emplace_back(handle.index);
        }

        bool isValid(ObjectHandle handle) const noexcept
        {
            return handle.index < mVersions.size() && mVersions[handle.index] == handle.version;
        }

    private:
        struct Chunk
        {
            struct Slot { alignas(T) uint8_t storage[sizeof(T)]; };
            std::array<Slot, kChunkCapacity> slots;
        };
        std::vector<std::unique_ptr<Chunk>> mChunks;
        std::vector<uint32_t> mVersions;
        std::vector<uint32_t> mFreeIndices;
    };
}
