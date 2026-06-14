#pragma once
#include "../Special.h"

namespace hyp
{
    template <typename T1, typename T2>
    struct container_traits<std::pair<T1, T2>> {
        using type1 = T1;
        using type2 = T2;
    };

    class StdPair : public Special
    {
    public:
        virtual Type* getFirstType() const = 0;
        virtual Type* getSecondType() const = 0;
        virtual void setFirstValue(void* instance, const void* value) const = 0;
        virtual void setSecondValue(void* instance, const void* value) const = 0;
        virtual void* getFirstPtr(void* instance) const = 0;
        virtual const void* getFirstPtr(const void* instance) const = 0;
        virtual void* getSecondPtr(void* instance) const = 0;
        virtual const void* getSecondPtr(const void* instance) const = 0;

    protected:
        StdPair(HString name, size_t size) : Special(name, size, Case::StdPair) {}
    };

    template <typename T> requires is_instance_of_v<T, std::pair>
    class TStdPair : public StdPair
    {
        friend class Reflection;
        using First = container_traits_t1<T>;
        using Second = container_traits_t2<T>;
    public:
        virtual void construct(void* instance) const override
        {
            std::construct_at(static_cast<std::pair<First, Second>*>(instance));
        }

        virtual void destruct(void* instance) const override
        {
            std::destroy_at(static_cast<std::pair<First, Second>*>(instance));
        }

        virtual bool compare(const void* instance1, const void* instance2) const override
        {
            const First* first1 = &(static_cast<const std::pair<First, Second>*>(instance1)->first);
            const First* first2 = &(static_cast<const std::pair<First, Second>*>(instance2)->first);
            const Second* second1 = &(static_cast<const std::pair<First, Second>*>(instance1)->second);
            const Second* second2 = &(static_cast<const std::pair<First, Second>*>(instance2)->second);

            bool firstEqual, secondEqual;

            if constexpr (Comparable<First, First> && !IsSpecialType<First>)
                firstEqual = *first1 == *first2;
            else
                firstEqual = Reflection::getType<First>()->compare(first1, first2);

            if constexpr (Comparable<Second, Second> && !IsSpecialType<Second>)
                secondEqual = *second1 == *second2;
            else
                secondEqual = Reflection::getType<Second>()->compare(second1, second2);

            return firstEqual && secondEqual;
        }

        virtual Type* getFirstType() const override
        {
            return Reflection::getType<First>();
        }

        virtual Type* getSecondType() const override
        {
            return Reflection::getType<Second>();
        }

        virtual void setFirstValue(void* instance, const void* value) const override
        {
            static_cast<std::pair<First, Second>*>(instance)->first = *(const First*)(value);
        }

        virtual void setSecondValue(void* instance, const void* value) const override
        {
            static_cast<std::pair<First, Second>*>(instance)->second = *(const Second*)(value);
        }

        virtual void* getFirstPtr(void* instance) const override
        {
            return &(static_cast<std::pair<First, Second>*>(instance)->first);
        }

        virtual const void* getFirstPtr(const void* instance) const override
        {
            return &(static_cast<const std::pair<First, Second>*>(instance)->first);
        }

        virtual void* getSecondPtr(void* instance) const override
        {
            return &(static_cast<std::pair<First, Second>*>(instance)->second);
        }

        virtual const void* getSecondPtr(const void* instance) const override
        {
            return &(static_cast<const std::pair<First, Second>*>(instance)->second);
        }

    private:
        static HString getName()
        {
            std::string name = "std::pair<" + std::string(Reflection::getType<First>()->getName().toStringView()) + ", " + std::string(Reflection::getType<Second>()->getName().toStringView()) + ">";
            return HString(name);
        }

        TStdPair() : StdPair(getName(), sizeof(std::pair<First, Second>)) {}
    };
}
