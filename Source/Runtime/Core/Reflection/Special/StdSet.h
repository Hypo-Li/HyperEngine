#pragma once
#include "../Special.h"

namespace hyp
{
    template <typename T>
    struct container_traits<std::set<T>> {
        using type = T;
    };

    class StdSet : public Special
    {
    public:
        virtual Type* getElementType() const = 0;
        virtual size_t getElementCount(const void* instance) const = 0;
        virtual std::vector<const void*> getElementPtrs(const void* instance) const = 0;
        virtual void insertElement(void* instance, const void* element) const = 0;
        virtual void removeElement(void* instance, const void* element) const = 0;

    protected:
        StdSet(HString name, size_t size) : Special(name, size, Case::StdSet) {}
    };

    template <typename T> requires is_instance_of_v<T, std::set>
    class TStdSet : public StdSet
    {
        friend class Reflection;
        using Element = container_traits_t<T>;
    public:
        virtual void construct(void* instance) const override
        {
            std::construct_at(static_cast<std::set<Element>*>(instance));
        }

        virtual void destruct(void* instance) const override
        {
            std::destroy_at(static_cast<std::set<Element>*>(instance));
        }

        virtual bool compare(const void* instance1, const void* instance2) const override
        {
            if (static_cast<const std::set<Element>*>(instance1)->size() == 0 &&
                static_cast<const std::set<Element>*>(instance2)->size() == 0)
                return true;
            return false;
        }

        virtual Type* getElementType() const override
        {
            return Reflection::getType<Element>();
        }

        virtual size_t getElementCount(const void* instance) const override
        {
            return static_cast<const std::set<Element>*>(instance)->size();
        }

        virtual std::vector<const void*> getElementPtrs(const void* instance) const override
        {
            const std::set<Element>* set = static_cast<const std::set<Element>*>(instance);
            std::vector<const void*> result(set->size());
            size_t i = 0;
            for (const Element& element : *set)
                result[i++] = &element;
            return result;
        }

        virtual void insertElement(void* instance, const void* element) const override
        {
            static_cast<std::set<Element>*>(instance)->insert(*static_cast<const Element*>(element));
        }

        virtual void removeElement(void* instance, const void* element) const override
        {
            std::set<Element>* set = static_cast<std::set<Element>*>(instance);
            auto findResult = set->find(*static_cast<const Element*>(element));
            if (findResult != set->end())
                set->erase(findResult);
        }

    private:
        static HString getName()
        {
            std::string name = "std::set<" + std::string(Reflection::getType<Element>()->getName().toStringView()) + ">";
            return HString(name);
        }

        TStdSet() : StdSet(getName(), sizeof(std::set<Element>)) {}
    };
}
