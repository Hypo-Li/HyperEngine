#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace hyp
{
    // 管理 [0, capacity) 这段整数下标区间的连续子区间分配——分配单位是"下标个数",
    // 不是字节/指针, 用来给共享 GPU buffer 里变长的数据块(比如若干个 uint32 组成的一段)
    // 分配偏移, 跟 Core/Memory/Allocator.h 里那套面向裸指针的 CPU 内存分配器是不同的
    // 抽象层级, 不共用。
    //
    // first-fit + 归还时合并相邻空闲区间, 不做压缩整理——分配失败只代表"当前没有一段
    // 足够大的连续空闲区间", 不代表总空闲量不够(可能是碎片化的), 调用方需要自己决定
    // 失败时怎么办(比如丢弃这次请求、或者把容量开大一点留余量)。
    class RangeAllocator
    {
    public:
        explicit RangeAllocator(uint32_t capacity);

        // 成功分配 count 个连续下标就返回起始偏移; 找不到足够大的连续空闲区间返回
        // nullopt。count == 0 总是成功(调用方比如"这张卡这一刻 0 页"不需要为这个特例
        // 单独判断)
        std::optional<uint32_t> allocate(uint32_t count) noexcept;

        void free(uint32_t offset, uint32_t count) noexcept;

        uint32_t getCapacity() const noexcept
        {
            return mCapacity;
        }

    private:
        uint32_t mCapacity;

        // 按 offset 升序排列的空闲区间列表(offset, count), 列表里任意两个区间既不重叠
        // 也不相邻(相邻的会在 free() 里被立即合并成一个), 否则碎片会长期累积成"一堆挨在
        // 一起、但没被识别成同一段"的小区间, 让本该能满足的分配请求提前失败
        std::vector<std::pair<uint32_t, uint32_t>> mFreeRanges;
    };
}
