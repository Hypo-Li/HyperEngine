#pragma once
#include "StagingAllocator.h"

#include <Core/Common/Refable.h>
#include <Core/Container/RangeAllocator.h>
#include <RHI/Buffer.h>
#include <RHI/CommandList.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace hyp
{
    class RenderBackend;

    // 固定容量的 GPU storage buffer 加连续区间分配器, 数据不在 CPU 保留副本。
    class RangeTableBase
    {
    public:
        RangeTableBase(RenderBackend& renderBackend, uint32_t capacity, uint32_t elementSize, HString debugName);

        ~RangeTableBase();

        RangeTableBase(const RangeTableBase&) = delete;
        RangeTableBase& operator=(const RangeTableBase&) = delete;

        // 返回起始偏移(元素为单位, 不是字节), 容量不够返回 nullopt
        std::optional<uint32_t> allocate(uint32_t count)
        {
            return mAllocator.allocate(count);
        }

        rhi::Buffer* getBuffer() const noexcept
        {
            return mBuffer.get();
        }

    protected:
        struct UploadView
        {
            uint32_t offset; // 元素为单位, 不是字节
            const void* data;
            size_t byteSize;
        };

        void freeRange(uint32_t offset, uint32_t count) noexcept
        {
            mAllocator.free(offset, count);
        }

        void prepareBuffer(rhi::CommandList& cmdList);

        // 返回已经处理掉的前缀条数, staging 预算不够时小于 uploads.size()
        size_t flushUploads(rhi::CommandList& cmdList, std::span<const UploadView> uploads);

    private:
        RenderBackend& mRenderBackend;
        uint32_t mElementSize;
        RangeAllocator mAllocator;
        TRef<rhi::Buffer> mBuffer;
        bool mInitialized = false;
    };

    template <typename T>
    class TRangeTable : public RangeTableBase
    {
        static_assert(std::is_trivially_copyable_v<T>);

    public:
        TRangeTable(RenderBackend& renderBackend, uint32_t capacity, const char* debugName)
            : RangeTableBase(renderBackend, capacity, sizeof(T), debugName)
        {
        }

        // 同时丢弃起点落在这段区间上还没 flush 的上传
        void free(uint32_t offset, uint32_t count) noexcept
        {
            std::erase_if(mPendingUploads, [offset, count](const PendingUpload& upload)
                {
                    return upload.offset >= offset && upload.offset < offset + count;
                });
            freeRange(offset, count);
        }

        // 同一偏移上还没 flush 的上传: 新数据不短于旧数据就整段替换, 否则覆盖旧数据的前缀
        void queueUpload(uint32_t offset, std::vector<T>&& data)
        {
            if (data.empty())
                return;

            auto existing = std::find_if(mPendingUploads.begin(), mPendingUploads.end(),
                [offset](const PendingUpload& upload) { return upload.offset == offset; });
            if (existing == mPendingUploads.end())
            {
                mPendingUploads.push_back(PendingUpload{ offset, std::move(data) });
            }
            else if (data.size() >= existing->data.size())
            {
                existing->data = std::move(data);
            }
            else
            {
                std::copy(data.begin(), data.end(), existing->data.begin());
            }
        }

        void queueUpload(uint32_t offset, std::span<const T> data)
        {
            queueUpload(offset, std::vector<T>(data.begin(), data.end()));
        }

        void prepare(rhi::CommandList& cmdList)
        {
            prepareBuffer(cmdList);
            if (mPendingUploads.empty())
                return;

            mUploadViews.clear();
            for (const PendingUpload& upload : mPendingUploads)
                mUploadViews.push_back(UploadView{ upload.offset, upload.data.data(), upload.data.size() * sizeof(T) });

            const size_t flushedCount = flushUploads(cmdList, mUploadViews);
            mPendingUploads.erase(mPendingUploads.begin(), mPendingUploads.begin() + flushedCount);
        }

        // staging 预算不够时 prepare() 一帧传不完, 采样缓冲前要先确认这里返回 false
        bool hasPendingUploads() const noexcept
        {
            return !mPendingUploads.empty();
        }

    private:
        struct PendingUpload
        {
            uint32_t offset;
            std::vector<T> data;
        };
        std::vector<PendingUpload> mPendingUploads;
        std::vector<UploadView> mUploadViews;
    };
}
