#pragma once
#include <RenderBase/RangeTable.h>
#include <RenderBase/StagingAllocator.h>

#include <Core/Common/Refable.h>
#include <Core/Math/Matrix.h>
#include <RHI/Buffer.h>
#include <RHI/BindGroup.h>
#include <RHI/CommandList.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace hyp
{
    class RenderBackend;

    struct SkinningHandle
    {
        uint32_t slot;
    };

    // 每个蒙皮实例占一个槽位, 槽位里是一对相邻区间交替当 current/prev, 以及帧边界的收尾状态。
    class SkinningManager
    {
    public:
        struct Allocation
        {
            SkinningHandle handle;
            uint32_t base; // 初始的 current 和 prev 都指向它
        };

        struct BaseChange
        {
            uint32_t ownerId;
            uint32_t currentBase;
            uint32_t prevBase;
        };

        // matrixCapacity 是所有蒙皮实例骨骼数之和的上限, 物理表大小为它的两倍
        SkinningManager(RenderBackend& renderBackend, uint32_t matrixCapacity);

        SkinningManager(const SkinningManager&) = delete;
        SkinningManager& operator=(const SkinningManager&) = delete;

        // ownerId 原样回传给 takeBaseChanges, 这里不解释它
        std::optional<Allocation> allocate(uint32_t boneCount, uint32_t ownerId);

        void free(SkinningHandle handle) noexcept;

        // 已上传过就翻转并写进新的 current 半区, 否则写进当前半区且 prev 等于 current
        void update(SkinningHandle handle, std::vector<float3x4>&& matrices);

        // 下一次 update 按"第一次上传"处理
        void markDiscontinuous(SkinningHandle handle) noexcept;

        // 每帧调用一次, 追加 current/prev 基址有变化的槽位; 上一帧更新过而本帧没更新的槽位会把 prev 收尾成等于 current
        void takeBaseChanges(std::vector<BaseChange>& out);

        void prepare(rhi::CommandList& cmdList);

        bool hasPendingUploads() const noexcept
        {
            return mMatrixTable.hasPendingUploads();
        }

        const rhi::Buffer& getMatrixBuffer() const noexcept
        {
            return *mMatrixTable.getBuffer();
        }

    private:
        struct Slot
        {
            uint32_t ownerId = 0;
            uint32_t rangeBase = 0;
            uint32_t boneCount = 0;
            uint32_t currentHalf = 0;
            bool inUse = false;
            bool initialized = false;
            bool prevIsCurrent = true;
            bool updatedThisFrame = false;
            bool updatedLastFrame = false;
            bool active = false;
        };

        static uint32_t getCurrentBase(const Slot& slot) noexcept;

        static uint32_t getPrevBase(const Slot& slot) noexcept;

    private:
        TRangeTable<float3x4> mMatrixTable;
        std::vector<Slot> mSlots;
        std::vector<uint32_t> mFreeSlots;
        std::vector<uint32_t> mActiveSlots; // updatedThisFrame 或 updatedLastFrame 的槽位
    };
}
