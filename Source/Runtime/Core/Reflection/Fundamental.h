#pragma once
#include "Type.h"
namespace hyp
{
    class Reflection;
    class Fundamental : public Type
    {
    protected:
        Fundamental(HString name, size_t size) :
            Type(name, size, Kind::Fundamental) {}
    };

    template <typename T> requires std::is_fundamental_v<T>
    class TFundamental : public Fundamental
    {
        friend class Reflection;
    public:
        virtual void construct(void* instance) const override
        {
        }

        virtual void destruct(void* instance) const override
        {
        }

        virtual bool compare(const void* instance1, const void* instance2) const override
        {
            if constexpr (std::is_void_v<T>)
                return true;
            else
                return *static_cast<const T*>(instance1) == *static_cast<const T*>(instance2);
        }

    private:
        static constexpr size_t sizeOf()
        {
            if constexpr (std::is_void_v<T>)
                return 0;
            else
                return sizeof(T);
        }

        TFundamental(HString name) :
            Fundamental(name, sizeOf()) {}
    };
}
