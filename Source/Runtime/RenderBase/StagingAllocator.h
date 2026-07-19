#pragma once
#include <optional>
#include <vector>

#include <RHI/Buffer.h>
#include <RHI/Device.h>

namespace hyp
{
    struct StagingAllocation
    {
        const rhi::Buffer* buffer;
        uint64_t offset;
        uint64_t size;
        void* mappedData;
    };

    class StagingAllocator
    {
    public:
        using Allocation = StagingAllocation;

        StagingAllocator(const rhi::Device& device, uint32_t pageSize, uint32_t maxPageCount);

        ~StagingAllocator();

        std::optional<Allocation> tryAllocate(size_t size, uint32_t alignment = 16);

        Allocation forceAllocate(size_t size, uint32_t alignment = 16);

        void commitAllocations(uint64_t signalValue);

        void reclaimAllocations(uint64_t currentValue);

    private:
        std::optional<Allocation> tryAllocateFromPages(size_t size, uint32_t alignment);

        Allocation allocateTransient(size_t size);

    private:
        const rhi::Device& mDevice;
        uint32_t mPageSize;
        uint32_t mMaxPageCount;

        struct Page
        {
            TRef<rhi::Buffer> buffer;
            uint32_t offset = 0;
            bool isPending = false;
            void* mappedData = nullptr;
        };
        std::vector<Page> mPages;

        struct PageRecord
        {
            uint32_t pageIndex = 0;
            uint64_t signalValue = ~0ull;
        };
        std::vector<PageRecord> mPendingPages;
        std::vector<PageRecord> mSubmittedPages;

        struct TransientRecord
        {
            TRef<rhi::Buffer> buffer;
            uint64_t signalValue = ~0ull;
        };
        std::vector<TransientRecord> mPendingTransients;
        std::vector<TransientRecord> mSubmittedTransients;
    };
}
