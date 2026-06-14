#pragma once
#include <type_traits>
#include "../Common/HString.h"

namespace hyp
{
    template<typename T, typename U>
    concept Comparable = requires(T a, U b) {
        { a == b } -> std::convertible_to<bool>;
    };

    class Type
    {
    public:
        enum class Kind
        {
            Fundamental,
            Enumeration,
            Structure,
            Class,
            Special,
        };

        HString getName() const
        {
            return mName;
        }

        size_t getSize() const
        {
            return mSize;
        }

        Kind getKind() const
        {
            return mKind;
        }

        virtual void construct(void* instance) const = 0;

        virtual void destruct(void* instance) const = 0;

        virtual bool compare(const void* instance1, const void* instance2) const = 0;

    protected:
        Type(HString name, size_t size, Kind kind) :
            mName(name), mSize(size), mKind(kind) {}

        virtual ~Type() = default;

    private:
        HString mName;
        size_t mSize;
        Kind mKind;
    };
}
