#include "Enumeration.h"

namespace hyp
{
    size_t Enumeration::getIndexByName(HString name) const noexcept
    {
        size_t count = mValues.size();
        for (size_t i = 0; i < count; ++i)
        {
            if (mValues[i].first == name)
                return i;
        }
        return count;
    }

    size_t Enumeration::getIndexByValue(int64_t value) const noexcept
    {
        size_t count = mValues.size();
        for (size_t i = 0; i < count; ++i)
        {
            if (mValues[i].second == value)
                return i;
        }
        return count;
    }

    HString Enumeration::getNameByIndex(size_t index) const noexcept
    {
        if (index < mValues.size())
        {
            return mValues[index].first;
        }
        return "";
    }

    int64_t Enumeration::getValueByIndex(size_t index) const noexcept
    {
        if (index < mValues.size())
        {
            return mValues[index].second;
        }
        return 0;
    }

    HString Enumeration::getNameByValue(int64_t value) const noexcept
    {
        size_t index = getIndexByValue(value);
        if (index < mValues.size())
        {
            return mValues[index].first;
        }
        return "";
    }

    int64_t Enumeration::getValueByName(HString name) const noexcept
    {
        size_t index = getIndexByName(name);
        if (index != mValues.size())
        {
            return mValues[index].second;
        }
        return 0;
    }

    Enumeration::Enumeration(HString name, size_t size, bool isBitMask)
        : Type(name, size, Kind::Enumeration), mIsBitMask(isBitMask)
    {
    }

}
