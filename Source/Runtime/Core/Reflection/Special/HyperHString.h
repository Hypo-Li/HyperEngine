#pragma once
#include "../Special.h"

namespace hyp
{
    class HyperHString : public Special
    {
        friend class Reflection;
    public:
        virtual void construct(void* instance) const override
        {
            std::construct_at(static_cast<HString*>(instance));
        }

        virtual void destruct(void* instance) const override
        {
            std::destroy_at(static_cast<HString*>(instance));
        }

        virtual bool compare(const void* lhs, const void* rhs) const override
        {
            return *static_cast<const HString*>(lhs) == *static_cast<const HString*>(rhs);
        }

        std::string_view toStringView(const void* instance) const
        {
            return static_cast<const HString*>(instance)->toStringView();
        }

        const char* toCString(const void* instance) const
        {
            return static_cast<const HString*>(instance)->toCString();
        }

    private:
        HyperHString() : Special("HString", sizeof(HString), Case::HyperHString) {}
    };
}
