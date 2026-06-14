#pragma once
#include <vector>
#include <span>
#include "Property.h"
#include "../Object/ObjectMemoryPool.h"
#include "../Common/Refable.h"

namespace hyp
{
    template <ObjectType T>
    TRef<T> createObject(Object* owner, ObjectFlags flags);

    class Reflection;

    class Class : public Type
    {
        friend class Reflection;
    public:
        virtual Object* allocate(ObjectHandle& handle) = 0;

        virtual void free(ObjectHandle handle) = 0;

        virtual bool isValid(ObjectHandle handle) const noexcept = 0;

        virtual bool isAbstract() const noexcept = 0;

        Class* getBaseClass()
        {
            return mBaseClass;
        }

        Property* getProperty(HString name) const;

        std::span<Property* const> getProperties() const
        {
            return mProperties;
        }

        const Object* getDefaultObject() const
        {
            return mDefaultObject;
        }

        bool isDerivedFrom(Class* clazz) const;

        bool isBaseOf(Class* clazz) const;

        std::span<Class* const> getDerivedClasses() const
        {
            return mDerivedClasses;
        }

    protected:
        Class(HString name, size_t size, Class* baseClass, TRef<Object> defaultObject);

        static void addDerivedToBase(Class* baseClass, Class* derivedClass);

        std::vector<Property*> mProperties;

    private:
        Class* mBaseClass;
        std::vector<Class*> mDerivedClasses;
        TRef<Object> mDefaultObject;
    };

    template <ObjectType T, ObjectType Base = Object, AllocateFrequency ALLOC_FREQ = AllocateFrequency::Medium>
    class TClass : public Class
    {
        friend class Reflection;
    public:
        virtual void construct(void* instance) const override;

        virtual void destruct(void* instance) const override;

        virtual bool compare(const void* instance1, const void* instance2) const override;

        virtual T* allocate(ObjectHandle& handle) override;

        virtual void free(ObjectHandle handle) override;

        virtual bool isValid(ObjectHandle handle) const noexcept override;

        virtual bool isAbstract() const noexcept override;

        template <typename Declared>
        Property* addProperty(HString name, Declared T::* memberPtr, CopyPolicy copyPolicy = CopyPolicy::Shallow);

    private:
        TClass(HString name);

        static Class* findBaseClass();

        static TRef<Object> createDefaultObject();

    private:
        ObjectMemoryPool<T, ALLOC_FREQ> mObjectMemoryPool;
    };
}
