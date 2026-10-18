#include "SurfaceCachePageAllocator.h"

#include <algorithm>
#include <cassert>

namespace hyp
{
    namespace
    {
        // surfaceCacheSizeFromSizeClass() 的反函数
        int32_t surfaceCacheSizeClassFromSize(int32_t size) noexcept
        {
            int32_t sizeClass = 0;
            for (int32_t s = kSurfaceCacheSubPageMinSize; s < size; s <<= 1)
                ++sizeClass;
            assert(surfaceCacheSizeFromSizeClass(sizeClass) == size && "size 必须是 kSurfaceCacheSubPageMinSize..kSurfaceCachePageSize 之间的 2 的幂");
            return sizeClass;
        }
    }

    SurfaceCachePageRegion getSurfaceCachePageRegion(int32_t pagesPerAxis, const SurfaceCachePageAllocation& allocation) noexcept
    {
        const int32_t pageIndex = static_cast<int32_t>(allocation.pageIndex);
        const int2 pageOrigin = int2(pageIndex % pagesPerAxis, pageIndex / pagesPerAxis) * kSurfaceCachePageSize;

        SurfaceCachePageRegion region;
        if (isWholePage(allocation))
        {
            region.pixelOrigin = pageOrigin;
            region.pixelExtent = int2(kSurfaceCachePageSize, kSurfaceCachePageSize);
            return region;
        }

        const int32_t width = surfaceCacheSizeFromSizeClass(allocation.widthClass);
        const int32_t height = surfaceCacheSizeFromSizeClass(allocation.heightClass);
        const int32_t subGridX = kSurfaceCachePageSize / width;
        const int32_t subSlotIndex = static_cast<int32_t>(allocation.subSlotIndex);
        const int2 subCoord(subSlotIndex % subGridX, subSlotIndex / subGridX);

        region.pixelOrigin = pageOrigin + subCoord * int2(width, height);
        region.pixelExtent = int2(width, height);
        return region;
    }

    SurfaceCachePageAllocator::SurfaceCachePageAllocator(int32_t resolution) :
        mPagesPerAxis(resolution / kSurfaceCachePageSize)
    {
        assert(resolution % kSurfaceCachePageSize == 0 && "分辨率必须是页尺寸的整数倍");

        // 逆序压入, 让从尾部取出的页号从小到大
        const uint32_t pageCapacity = getPageCapacity();
        mFreePages.reserve(pageCapacity);
        for (uint32_t i = pageCapacity; i > 0; --i)
            mFreePages.push_back(i - 1);
    }

    std::optional<SurfaceCachePageAllocation> SurfaceCachePageAllocator::allocateWholePage() noexcept
    {
        if (mFreePages.empty())
            return std::nullopt;

        const uint32_t pageIndex = mFreePages.back();
        mFreePages.pop_back();

        return SurfaceCachePageAllocation{ pageIndex, kSurfaceCacheFullPageSizeClass, kSurfaceCacheFullPageSizeClass, 0u };
    }

    SurfaceCachePageAllocation SurfaceCachePageAllocator::allocateFromBinPage(SubPageBinPage& page, int32_t widthClass, int32_t heightClass) noexcept
    {
        assert(page.freeSlotCount > 0 && "调用方必须先确认这一页还有空子槽");

        uint32_t subSlotIndex = 0;
        for (; subSlotIndex < page.occupied.size(); ++subSlotIndex)
        {
            if (!page.occupied[subSlotIndex])
                break;
        }
        assert(subSlotIndex < page.occupied.size());
        page.occupied[subSlotIndex] = 1u;
        --page.freeSlotCount;

        return SurfaceCachePageAllocation{ page.pageIndex, widthClass, heightClass, subSlotIndex };
    }

    std::optional<SurfaceCachePageAllocation> SurfaceCachePageAllocator::allocateSinglePage(int32_t widthTexels, int32_t heightTexels)
    {
        if (widthTexels == kSurfaceCachePageSize && heightTexels == kSurfaceCachePageSize)
            return allocateWholePage();

        const int32_t widthClass = surfaceCacheSizeClassFromSize(widthTexels);
        const int32_t heightClass = surfaceCacheSizeClassFromSize(heightTexels);
        std::vector<SubPageBinPage>& bin = mSubPageBins[widthClass][heightClass];

        for (SubPageBinPage& page : bin)
        {
            if (page.freeSlotCount > 0)
                return allocateFromBinPage(page, widthClass, heightClass);
        }

        if (mFreePages.empty())
            return std::nullopt;
        const uint32_t pageIndex = mFreePages.back();
        mFreePages.pop_back();

        const int32_t subGridX = kSurfaceCachePageSize / widthTexels;
        const int32_t subGridY = kSurfaceCachePageSize / heightTexels;

        SubPageBinPage newPage;
        newPage.pageIndex = pageIndex;
        newPage.freeSlotCount = static_cast<uint32_t>(subGridX * subGridY);
        newPage.occupied.assign(static_cast<size_t>(subGridX) * static_cast<size_t>(subGridY), 0u);
        bin.push_back(std::move(newPage));

        return allocateFromBinPage(bin.back(), widthClass, heightClass);
    }

    std::vector<SurfaceCachePageAllocation> SurfaceCachePageAllocator::allocate(int32_t widthTexels, int32_t heightTexels)
    {
        assert(widthTexels >= kSurfaceCacheSubPageMinSize && heightTexels >= kSurfaceCacheSubPageMinSize
            && "分辨率不应该比子页最小尺寸还小, 调用方在算分辨率时就该 clamp 到这个下限");

        if (widthTexels <= kSurfaceCachePageSize && heightTexels <= kSurfaceCachePageSize)
        {
            std::optional<SurfaceCachePageAllocation> allocation = allocateSinglePage(widthTexels, heightTexels);
            if (!allocation)
                return {};
            return { *allocation };
        }

        // 与 UE FLumenCard::GetMipMapDesc() 的 clamp 一致: 没跨页的那根轴不再尝试塞进子页 bin
        const int32_t pageCountX = std::max(1, (widthTexels + kSurfaceCachePageSize - 1) / kSurfaceCachePageSize);
        const int32_t pageCountY = std::max(1, (heightTexels + kSurfaceCachePageSize - 1) / kSurfaceCachePageSize);
        const uint32_t pageCount = static_cast<uint32_t>(pageCountX) * static_cast<uint32_t>(pageCountY);

        std::vector<SurfaceCachePageAllocation> allocations;
        allocations.reserve(pageCount);
        for (uint32_t i = 0; i < pageCount; ++i)
        {
            std::optional<SurfaceCachePageAllocation> allocation = allocateWholePage();
            if (!allocation)
            {
                free(allocations);
                return {};
            }
            allocations.push_back(*allocation);
        }
        return allocations;
    }

    void SurfaceCachePageAllocator::free(std::span<const SurfaceCachePageAllocation> allocations)
    {
        for (const SurfaceCachePageAllocation& allocation : allocations)
        {
            if (isWholePage(allocation))
            {
                mFreePages.push_back(allocation.pageIndex);
                continue;
            }

            std::vector<SubPageBinPage>& bin = mSubPageBins[allocation.widthClass][allocation.heightClass];
            auto it = std::find_if(bin.begin(), bin.end(),
                [&allocation](const SubPageBinPage& page) { return page.pageIndex == allocation.pageIndex; });
            assert(it != bin.end() && "释放的分配在这个分配器的子页 bin 里找不到对应的物理页, 传了不是它分配出去的 SurfaceCachePageAllocation");

            it->occupied[allocation.subSlotIndex] = 0u;
            ++it->freeSlotCount;

            if (it->freeSlotCount == static_cast<uint32_t>(it->occupied.size()))
            {
                mFreePages.push_back(it->pageIndex);
                bin.erase(it);
            }
        }
    }
}
