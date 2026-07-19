#pragma once
#include "Type.h"

#include <vector>
#include <span>

namespace hyp
{
    class Enumeration : public Type
    {
    public:
        virtual void setValue(void* instance, uint64_t value) const = 0;
        virtual uint64_t getValue(const void* instance) const = 0;

        bool isBitMask() const noexcept
        {
            return mIsBitMask;
        }

        size_t getValueCount() const noexcept
        {
            return mValues.size();
        }

        size_t getIndexByName(HString name) const noexcept;

        size_t getIndexByValue(uint64_t value) const noexcept;

        HString getNameByIndex(size_t index) const noexcept;

        uint64_t getValueByIndex(size_t index) const noexcept;

        HString getNameByValue(uint64_t value) const noexcept;

        uint64_t getValueByName(HString name) const noexcept;

        std::vector<HString> getFlagNames(const void* instance) const;

        void setFlagsByNames(void* instance, std::span<HString> names) const;

    protected:
        Enumeration(HString name, size_t size, bool isBitMask);

        bool mIsBitMask = false;
        std::vector<std::pair<HString, uint64_t>> mValues;
    };

    class Reflection;
    template <typename T> requires std::is_enum_v<T>
    class TEnumeration : public Enumeration
    {
        friend class Reflection;
        using UnderlyingType = std::underlying_type_t<T>;
    public:
        virtual void construct(void* instance) const override
        {
        }

        virtual void destruct(void* instance) const override
        {
        }

        virtual bool compare(const void* instance1, const void* instance2) const override
        {
            return *static_cast<const T*>(instance1) == *static_cast<const T*>(instance2);
        }

        virtual void setValue(void* instance, uint64_t value) const override
        {
            *static_cast<UnderlyingType*>(instance) = static_cast<UnderlyingType>(value);
        }

        virtual uint64_t getValue(const void* instance) const override
        {
            return static_cast<const uint64_t>(*(const UnderlyingType*)(instance));
        }

        void addValue(HString name, T value)
        {
            mValues.emplace_back(name, static_cast<uint64_t>(value));
        }

    protected:
        TEnumeration(HString name, bool isBitMask = false) :
            Enumeration(name, sizeof(T), isBitMask)
        {
        }
    };
}
