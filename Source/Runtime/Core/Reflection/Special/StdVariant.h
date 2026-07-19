#pragma once
#include "../Special.h"

namespace hyp
{
    class StdVariant : public Special
    {
    public:
        virtual std::vector<Type*> getTypes() const = 0;
        virtual size_t getTypeIndex(const void* instance) const = 0;
        virtual void* getValuePtr(void* instance) const = 0;
        virtual const void* getValuePtr(const void* instance) const = 0;
        virtual void setValue(void* instance, size_t typeIndex, const void* value) const = 0;

    protected:
        StdVariant(HString name, size_t size) : Special(name, size, Case::StdVariant) {}
    };

    template <typename T> requires is_instance_of_v<T, std::variant>
    class TStdVariant : public StdVariant
    {
        friend class Reflection;
        template <std::size_t... Is>
        static std::vector<Type*> getTypesImpl(std::index_sequence<Is...>)
        {
            return { Reflection::getType<std::variant_alternative_t<Is, T>>()... };
        }

        template <std::size_t I>
        static void setValueImpl(T* variant, size_t typeIndex, const void* value)
        {
            if (I == typeIndex)
            {
                variant->emplace<I>(*(std::variant_alternative_t<I, T>*)(value));
                return;
            }

            if constexpr (I + 1 < std::variant_size_v<T>)
                setValueImpl<I + 1>(variant, typeIndex, value);
        }

        template <std::size_t I>
        static const void* getValuePtrImpl(const T* variant)
        {
            if constexpr (I < std::variant_size_v<T>)
            {
                auto* ptr = std::get_if<I>(variant);
                if (ptr)
                    return static_cast<const void*>(ptr);
                if constexpr (I + 1 < std::variant_size_v<T>)
                    return getValuePtrImpl<I + 1>(variant);
            }
            return nullptr;
        }

    public:
        virtual void construct(void* instance) const override
        {
            std::construct_at(static_cast<T*>(instance));
        }

        virtual void destruct(void* instance) const override
        {
            std::destroy_at(static_cast<T*>(instance));
        }

        virtual bool compare(const void* instance1, const void* instance2) const override
        {
            //uint32_t instance1TypeIndex = getTypeIndex(instance1);
            //if (instance1TypeIndex == getTypeIndex(instance2) && getTypes().at(instance1TypeIndex)->compare(getValuePtr(instance1), getValuePtr(instance2)))
            //    return true;
            return false;
        }

        virtual std::vector<Type*> getTypes() const override
        {
            return getTypesImpl(std::make_index_sequence<std::variant_size_v<T>>());
        }

        virtual size_t getTypeIndex(const void* instance) const override
        {
            return static_cast<const T*>(instance)->index();
        }

        virtual void* getValuePtr(void* instance) const override
        {
            return const_cast<void*>(getValuePtrImpl<0>(static_cast<T*>(instance)));
        }

        virtual const void* getValuePtr(const void* instance) const override
        {
            return getValuePtrImpl<0>(static_cast<const T*>(instance));
        }

        virtual void setValue(void* instance, size_t typeIndex, const void* value) const  override
        {
            setValueImpl<0>(static_cast<T*>(instance), typeIndex, value);
        }

    private:
        template <std::size_t... Is>
        static std::string getVariantTypeNames(std::index_sequence<Is...>)
        {
            std::string result;
            std::vector<HString> typeNames = { Reflection::getType<std::variant_alternative_t<Is, T>>()->getName()...};

            for (uint32_t i = 0; i < typeNames.size(); ++i)
            {
                result += typeNames[i].toStringView();
                if (i != typeNames.size() - 1)
                    result += ", ";
            }
            return result;
        }

        static HString getName()
        {
            std::string name = "std::variant<" + getVariantTypeNames(std::make_index_sequence<std::variant_size_v<T>>{}) + ">";
            return HString(name);
        }

        TStdVariant() : StdVariant(getName(), sizeof(T)) {}
    };
}
