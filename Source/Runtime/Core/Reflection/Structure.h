#pragma once
#include "Property.h"
#include <span>

namespace hyp
{
    class Reflection;

    class Structure : public Type
    {
        friend class Reflection;
    public:
        Property* getProperty(HString name) const
        {
            for (auto prop : mProperties)
                if (prop->getName() == name)
                    return prop;
            return nullptr;
        }

        std::span<Property* const> getProperties() const noexcept
        {
            return mProperties;
        }

        void setVersion(uint32_t version) noexcept
        {
            mVersion = version;
        }

        uint32_t getVersion() const noexcept
        {
            return mVersion;
        }

    protected:
        Structure(HString name, size_t size) :
            Type(name, size, Kind::Structure) {}

        std::vector<Property*> mProperties;
        uint32_t mVersion = 0;
    };

    template <typename T>
    class TStructure : public Structure
    {
        friend class Reflection;
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
            if constexpr (Comparable<T, T>)
                return *static_cast<const T*>(instance1) == *static_cast<const T*>(instance2);
            else
            {
                for (Property* prop : getProperties())
                    if (!prop->compare(instance1, instance2))
                        return false;
                return true;
            }
        }

        template <typename Declared>
        Property* addProperty(HString name, Declared T::* memberPtr, CopyPolicy copyPolicy = CopyPolicy::Shallow)
        {
            Property* property = new TProperty<Declared, T>(name, memberPtr, copyPolicy);
            mProperties.emplace_back(property);
            return property;
        }

    protected:
        TStructure(HString name) :
            Structure(name, sizeof(T)) {}
    };
}
