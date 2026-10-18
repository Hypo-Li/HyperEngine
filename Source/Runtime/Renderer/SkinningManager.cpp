#include "SkinningManager.h"
#include <RenderBase/RenderBackend.h>

#include <algorithm>

namespace hyp
{
    SkinningManager::SkinningManager(RenderBackend& renderBackend, uint32_t matrixCapacity)
        : mMatrixTable(renderBackend, matrixCapacity * 2, "SkinningManager.MatrixBuffer")
    {
    }

    uint32_t SkinningManager::getCurrentBase(const Slot& slot) noexcept
    {
        return slot.rangeBase + slot.currentHalf * slot.boneCount;
    }

    uint32_t SkinningManager::getPrevBase(const Slot& slot) noexcept
    {
        if (slot.prevIsCurrent)
            return getCurrentBase(slot);
        return slot.rangeBase + (slot.currentHalf ^ 1u) * slot.boneCount;
    }

    std::optional<SkinningManager::Allocation> SkinningManager::allocate(uint32_t boneCount, uint32_t ownerId)
    {
        std::optional<uint32_t> rangeBase = mMatrixTable.allocate(boneCount * 2);
        if (!rangeBase)
            return std::nullopt;

        uint32_t slotIndex;
        if (!mFreeSlots.empty())
        {
            slotIndex = mFreeSlots.back();
            mFreeSlots.pop_back();
        }
        else
        {
            slotIndex = static_cast<uint32_t>(mSlots.size());
            mSlots.emplace_back();
        }

        Slot& slot = mSlots[slotIndex];
        slot = Slot{};
        slot.ownerId = ownerId;
        slot.rangeBase = *rangeBase;
        slot.boneCount = boneCount;
        slot.inUse = true;
        return Allocation{ SkinningHandle{ slotIndex }, *rangeBase };
    }

    void SkinningManager::free(SkinningHandle handle) noexcept
    {
        Slot& slot = mSlots[handle.slot];
        mMatrixTable.free(slot.rangeBase, slot.boneCount * 2);

        if (slot.active)
            std::erase(mActiveSlots, handle.slot);

        slot = Slot{};
        mFreeSlots.push_back(handle.slot);
    }

    void SkinningManager::update(SkinningHandle handle, std::vector<float3x4>&& matrices)
    {
        Slot& slot = mSlots[handle.slot];
        if (slot.initialized)
        {
            slot.currentHalf ^= 1u;
            slot.prevIsCurrent = false;
        }
        else
        {
            slot.initialized = true;
            slot.prevIsCurrent = true;
        }

        mMatrixTable.queueUpload(getCurrentBase(slot), std::move(matrices));

        slot.updatedThisFrame = true;
        if (!slot.active)
        {
            slot.active = true;
            mActiveSlots.push_back(handle.slot);
        }
    }

    void SkinningManager::markDiscontinuous(SkinningHandle handle) noexcept
    {
        mSlots[handle.slot].initialized = false;
    }

    void SkinningManager::takeBaseChanges(std::vector<BaseChange>& out)
    {
        size_t keptCount = 0;
        for (uint32_t slotIndex : mActiveSlots)
        {
            Slot& slot = mSlots[slotIndex];
            if (slot.updatedThisFrame)
            {
                out.push_back({ slot.ownerId, getCurrentBase(slot), getPrevBase(slot) });
                slot.updatedThisFrame = false;
                slot.updatedLastFrame = true;
                mActiveSlots[keptCount++] = slotIndex;
            }
            else
            {
                slot.prevIsCurrent = true;
                out.push_back({ slot.ownerId, getCurrentBase(slot), getCurrentBase(slot) });
                slot.updatedLastFrame = false;
                slot.active = false;
            }
        }
        mActiveSlots.resize(keptCount);
    }

    void SkinningManager::prepare(rhi::CommandList& cmdList)
    {
        mMatrixTable.prepare(cmdList);
    }
}
