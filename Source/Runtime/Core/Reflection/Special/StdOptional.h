#pragma once
#include "../Special.h"

namespace hyp
{
    template <typename T>
    struct container_traits<std::optional<T>> {
        using type = T;
    };

    class StdOptional : public Special
    {
    public:
        virtual Type* getValueType() const = 0;
        virtual bool hasValue(const void* instance) const = 0;
        virtual void* getValuePtr(void* instance) const = 0;
        virtual const void* getValuePtr(const void* instance) const = 0;
        virtual void setValue(void* instance, const void* value) const = 0;
        virtual void reset(void* instance) const = 0;

    protected:
        StdOptional(HString name, size_t size) : Special(name, size, Case::StdOptional) {}
    };

    template <typename T> requires is_instance_of_v<T, std::optional>
    class TStdOptional : public StdOptional
    {
        friend class Reflection;
        using Element = container_traits_t<T>;
    public:
        virtual void construct(void* instance) const override
        {
            std::construct_at(static_cast<std::optional<Element>*>(instance));
        }

        virtual void destruct(void* instance) const override
        {
            std::destroy_at(static_cast<std::optional<Element>*>(instance));
        }

        virtual bool compare(const void* instance1, const void* instance2) const override
        {
            // 和 StdVector/StdSet 一样只判断"是否为空",避免为了 diff 属性强制比较内部值
            return !static_cast<const std::optional<Element>*>(instance1)->has_value() &&
                !static_cast<const std::optional<Element>*>(instance2)->has_value();
        }

        virtual Type* getValueType() const override
        {
            return Reflection::getType<Element>();
        }

        virtual bool hasValue(const void* instance) const override
        {
            return static_cast<const std::optional<Element>*>(instance)->has_value();
        }

        virtual void* getValuePtr(void* instance) const override
        {
            return &static_cast<std::optional<Element>*>(instance)->value();
        }

        virtual const void* getValuePtr(const void* instance) const override
        {
            return &static_cast<const std::optional<Element>*>(instance)->value();
        }

        virtual void setValue(void* instance, const void* value) const override
        {
            static_cast<std::optional<Element>*>(instance)->emplace(*static_cast<const Element*>(value));
        }

        virtual void reset(void* instance) const override
        {
            static_cast<std::optional<Element>*>(instance)->reset();
        }

    private:
        static HString getName()
        {
            std::string name = "std::optional<" + std::string(Reflection::getType<Element>()->getName().toStringView()) + ">";
            return HString(name);
        }

        TStdOptional() : StdOptional(getName(), sizeof(std::optional<Element>)) {}
    };
}
