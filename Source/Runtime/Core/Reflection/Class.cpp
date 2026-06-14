#include "Class.h"
#include "Reflection.h"
#include "../Object/Object.h"

namespace hyp
{
    Property* Class::getProperty(HString name) const
    {
        for (auto prop : mProperties)
            if (prop->getName() == name)
                return prop;
        return nullptr;
    }

    bool Class::isDerivedFrom(Class* clazz) const
    {
        Class* baseClass = mBaseClass;
        while (baseClass)
        {
            if (baseClass == clazz)
                return true;
            baseClass = baseClass->mBaseClass;
        }
        return false;
    }

    bool Class::isBaseOf(Class* clazz) const
    {
        Class* baseClass = clazz->mBaseClass;
        while (baseClass)
        {
            if (baseClass == this)
                return true;
            baseClass = baseClass->mBaseClass;
        }
        return false;
    }

    Class::Class(HString name, size_t size, Class* baseClass, TRef<Object> defaultObject)
        : Type(name, size, Kind::Class), mBaseClass(baseClass), mDefaultObject(defaultObject)
    {
    }

    void Class::addDerivedToBase(Class* baseClass, Class* derivedClass)
    {
        baseClass->mDerivedClasses.push_back(derivedClass);
    }

}
