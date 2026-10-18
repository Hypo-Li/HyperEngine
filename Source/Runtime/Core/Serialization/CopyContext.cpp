#include "CopyContext.h"

#include <cassert>

namespace hyp
{
    void CopyContext::registerExternalCopy(Object* original, Object* copy)
    {
        assert(mCopies.find(original) == mCopies.end() &&
            "CopyContext::registerExternalCopy: original 已经登记过副本");
        mCopies.emplace(original, TRef<Object>(copy));
    }

    Object* CopyContext::getOrCreateCopy(Object* original, Object* newOwner, bool& isNew)
    {
        auto findResult = mCopies.find(original);
        if (findResult != mCopies.end())
        {
            isNew = false;
            return findResult->second.get();
        }

        Class* clazz = original->getClass();
        TRef<Object> copy = createObject(clazz, newOwner);
        Object* copyPtr = copy.get();
        mCopies.emplace(original, std::move(copy));
        mAllocationOrder.push_back(original);
        isNew = true;
        return copyPtr;
    }

    Object* CopyContext::findCopy(Object* original) const
    {
        auto findResult = mCopies.find(original);
        return findResult != mCopies.end() ? findResult->second.get() : nullptr;
    }

    std::span<Object* const> CopyContext::takeNewlyAllocated()
    {
        std::span<Object* const> newlyAllocated(mAllocationOrder.data() + mConsumedCount, mAllocationOrder.size() - mConsumedCount);
        mConsumedCount = mAllocationOrder.size();
        return newlyAllocated;
    }
}
