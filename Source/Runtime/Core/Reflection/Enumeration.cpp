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

    size_t Enumeration::getIndexByValue(uint64_t value) const noexcept
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

    uint64_t Enumeration::getValueByIndex(size_t index) const noexcept
    {
        if (index < mValues.size())
        {
            return mValues[index].second;
        }
        return 0;
    }

    HString Enumeration::getNameByValue(uint64_t value) const noexcept
    {
        size_t index = getIndexByValue(value);
        if (index < mValues.size())
        {
            return mValues[index].first;
        }
        return "";
    }

    uint64_t Enumeration::getValueByName(HString name) const noexcept
    {
        size_t index = getIndexByName(name);
        if (index != mValues.size())
        {
            return mValues[index].second;
        }
        return 0;
    }

    std::vector<HString> Enumeration::getFlagNames(const void* instance) const
    {
        std::vector<HString> result;
        if (!mIsBitMask)
            return result;

        uint64_t value = getValue(instance);
        if (value == 0)
        {
            HString name = getNameByValue(0);
            if (!name.empty())
                result.push_back(name);
            return result;
        }

        for (const auto& pair : mValues)
        {
            if (pair.second != 0 && (value & pair.second) != 0)
                result.push_back(pair.first);
        }
        return result;
    }

    void Enumeration::setFlagsByNames(void* instance, std::span<HString> names) const
    {
        if (!mIsBitMask)
            return;

        uint64_t value = 0;
        for (const HString& name : names)
            value |= getValueByName(name);

        setValue(instance, value);
    }

    Enumeration::Enumeration(HString name, size_t size, bool isBitMask)
        : Type(name, size, Kind::Enumeration), mIsBitMask(isBitMask)
    {
    }

}
