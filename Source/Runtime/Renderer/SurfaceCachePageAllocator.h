#pragma once
#include <Core/Math/Vector.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace hyp
{
    // 与 brick 不同, card atlas 的页没有重叠边界, 页尺寸即标称值
    inline constexpr int32_t kSurfaceCachePageSize = 128;

    // 比此更小的卡也按此尺寸分配
    inline constexpr int32_t kSurfaceCacheSubPageMinSize = 8;

    inline constexpr int32_t surfaceCacheSizeFromSizeClass(int32_t sizeClass) noexcept
    {
        return kSurfaceCacheSubPageMinSize << sizeClass;
    }

    // 兼作"整页分配"的哨兵档位
    inline constexpr int32_t kSurfaceCacheFullPageSizeClass = 4;
    static_assert(surfaceCacheSizeFromSizeClass(kSurfaceCacheFullPageSizeClass) == kSurfaceCachePageSize,
        "档位换算公式和这个哨兵值必须始终对应到 kSurfaceCachePageSize, 两者改一个要跟着改另一个");

    // 整页分配时 widthClass/heightClass 取 kSurfaceCacheFullPageSizeClass, subSlotIndex 为 0
    struct SurfaceCachePageAllocation
    {
        uint32_t pageIndex;
        int32_t widthClass;
        int32_t heightClass;
        uint32_t subSlotIndex;
    };

    struct SurfaceCachePageRegion
    {
        int2 pixelOrigin;
        int2 pixelExtent;
    };

    inline bool isWholePage(const SurfaceCachePageAllocation& allocation) noexcept
    {
        return allocation.widthClass == kSurfaceCacheFullPageSizeClass
            && allocation.heightClass == kSurfaceCacheFullPageSizeClass;
    }

    SurfaceCachePageRegion getSurfaceCachePageRegion(int32_t pagesPerAxis, const SurfaceCachePageAllocation& allocation) noexcept;

    // 在一张 Surface Cache 图集内分配物理页与子页, 只管几何, 不管纹理和页表
    class SurfaceCachePageAllocator
    {
    public:
        explicit SurfaceCachePageAllocator(int32_t resolution);

        SurfaceCachePageAllocator(const SurfaceCachePageAllocator&) = delete;
        SurfaceCachePageAllocator& operator=(const SurfaceCachePageAllocator&) = delete;

        // 任一轴超过 kSurfaceCachePageSize 时两轴都按整页网格分配, 否则返回单个元素, 可能是子页; 失败返回空
        std::vector<SurfaceCachePageAllocation> allocate(int32_t widthTexels, int32_t heightTexels);

        void free(std::span<const SurfaceCachePageAllocation> allocations);

        SurfaceCachePageRegion getRegion(const SurfaceCachePageAllocation& allocation) const noexcept
        {
            return getSurfaceCachePageRegion(mPagesPerAxis, allocation);
        }

    private:
        // 要求 page.freeSlotCount > 0
        struct SubPageBinPage
        {
            uint32_t pageIndex;
            uint32_t freeSlotCount;
            std::vector<uint8_t> occupied;
        };

        uint32_t getPageCapacity() const noexcept
        {
            return static_cast<uint32_t>(mPagesPerAxis * mPagesPerAxis);
        }

        std::optional<SurfaceCachePageAllocation> allocateWholePage() noexcept;

        std::optional<SurfaceCachePageAllocation> allocateSinglePage(int32_t widthTexels, int32_t heightTexels);

        SurfaceCachePageAllocation allocateFromBinPage(SubPageBinPage& page, int32_t widthClass, int32_t heightClass) noexcept;

    private:
        int32_t mPagesPerAxis;

        std::vector<uint32_t> mFreePages;

        // 已满的页也保留在此以便归还时按页号查找, 清空后才还给 mFreePages
        std::vector<SubPageBinPage> mSubPageBins[kSurfaceCacheFullPageSizeClass][kSurfaceCacheFullPageSizeClass];
    };
}
