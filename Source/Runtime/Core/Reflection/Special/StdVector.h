#pragma once
#include "../Special.h"

namespace hyp
{
    template <typename T>
    struct container_traits<std::vector<T>> {
        using type = T;
    };

    class StdVector : public Special
    {
    public:
        virtual Type* getElementType() const = 0;
        virtual size_t getElementCount(const void* instance) const = 0;
        virtual void setElement(void* instance, size_t index, const void* data) const = 0;
        virtual void getElement(const void* instance, size_t index, void* data) const = 0;
        virtual void* getElementPtr(void* instance, size_t index) const = 0;
        virtual const void* getElementPtr(const void* instance, size_t index) const = 0;
        virtual void resize(void* instance, size_t size) const = 0;

    protected:
        StdVector(HString name, size_t size) : Special(name, size, Case::StdVector) {}
    };

    template <typename T> requires is_instance_of_v<T, std::vector>
    class TStdVector : public StdVector
    {
        friend class Reflection;
        using Element = container_traits_t<T>;
    public:
        virtual void construct(void* instance) const override
        {
            std::construct_at(static_cast<std::vector<Element>*>(instance));
        }

        virtual void destruct(void* instance) const override
        {
            std::destroy_at(static_cast<std::vector<Element>*>(instance));
        }

        virtual bool compare(const void* instance1, const void* instance2) const override
        {
            if (static_cast<const std::vector<Element>*>(instance1)->size() == 0 &&
                static_cast<const std::vector<Element>*>(instance2)->size() == 0)
                return true;
            return false;
        }

        virtual Type* getElementType() const override
        {
            return Reflection::getType<Element>();
        }

        virtual size_t getElementCount(const void* instance) const override
        {
            return static_cast<const std::vector<Element>*>(instance)->size();
        }

        virtual void setElement(void* instance, size_t index, const void* data) const override
        {
            static_cast<std::vector<Element>*>(instance)->at(index) = *(const Element*)(data);
        }

        virtual void getElement(const void* instance, size_t index, void* data) const override
        {
            *(Element*)(data) = static_cast<const std::vector<Element>*>(instance)->at(index);
        }

        virtual void* getElementPtr(void* instance, size_t index) const override
        {
            return &static_cast<std::vector<Element>*>(instance)->at(index);
        }

        virtual const void* getElementPtr(const void* instance, size_t index) const override
        {
            return &static_cast<const std::vector<Element>*>(instance)->at(index);
        }

        virtual void resize(void* instance, size_t size) const override
        {
            static_cast<std::vector<Element>*>(instance)->resize(size);
        }

    private:
        static HString getName()
        {
            std::string name = "std::vector<" + std::string(Reflection::getType<Element>()->getName().toStringView()) + ">";
            return HString(name);
        }

        TStdVector() : StdVector(getName(), sizeof(std::vector<Element>)) {}
    };
}
