#pragma once
#include "../Special.h"

namespace hyp
{
    class StdString : public Special
    {
        friend class Reflection;
    public:
        virtual void construct(void* instance) const override
        {
            std::construct_at(static_cast<std::string*>(instance));
        }

        virtual void destruct(void* instance) const override
        {
            std::destroy_at(static_cast<std::string*>(instance));
        }

        virtual bool compare(const void* instance1, const void* instance2) const override
        {
            return *static_cast<const std::string*>(instance1) == *static_cast<const std::string*>(instance2);
        }

    private:
        StdString() : Special("std::string", sizeof(std::string), Case::StdString) {}
    };
}
