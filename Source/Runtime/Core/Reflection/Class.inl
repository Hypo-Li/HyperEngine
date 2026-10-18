#include "Class.h"
namespace hyp
{
    template<ObjectType T, ObjectType Base, AllocateFrequency ALLOC_FREQ>
    inline void TClass<T, Base, ALLOC_FREQ>::construct(void* instance) const
    {
        if constexpr (!std::is_abstract_v<T>)
        {
            std::construct_at(static_cast<T*>(instance));
        }
    }

    template<ObjectType T, ObjectType Base, AllocateFrequency ALLOC_FREQ>
    inline void TClass<T, Base, ALLOC_FREQ>::destruct(void* instance) const
    {
        if constexpr (!std::is_abstract_v<T>)
        {
            std::destroy_at(static_cast<T*>(instance));
        }
    }

    template <ObjectType T, ObjectType Base, AllocateFrequency ALLOC_FREQ>
    bool TClass<T, Base, ALLOC_FREQ>::compare(const void* instance1, const void* instance2) const
    {
        if constexpr (Comparable<T, T>)
            return *static_cast<const T*>(instance1) == *static_cast<const T*>(instance2);
        else
        {
            Class* clazz = const_cast<Class*>(static_cast<const Class*>(this));
            while (clazz)
            {
                for (const Property* prop : clazz->getProperties())
                    if (!prop->compare(instance1, instance2))
                        return false;

                clazz = clazz->getBaseClass();
            }
            return true;
        }
    }

    template<ObjectType T, ObjectType Base, AllocateFrequency ALLOC_FREQ>
    inline T* TClass<T, Base, ALLOC_FREQ>::allocate(ObjectHandle& handle)
    {
        handle.clazz = this;
        return mObjectMemoryPool.allocate(handle);
    }

    template<ObjectType T, ObjectType Base, AllocateFrequency ALLOC_FREQ>
    inline void TClass<T, Base, ALLOC_FREQ>::free(ObjectHandle handle)
    {
        assert(handle.clazz == this);
        mObjectMemoryPool.free(handle);
    }

    template<ObjectType T, ObjectType Base, AllocateFrequency ALLOC_FREQ>
    inline bool TClass<T, Base, ALLOC_FREQ>::isValid(ObjectHandle handle) const noexcept
    {
        return handle.clazz == this && mObjectMemoryPool.isValid(handle);
    }

    template <ObjectType T, ObjectType Base, AllocateFrequency ALLOC_FREQ>
    template <typename Declared>
    TProperty<Declared, T>* TClass<T, Base, ALLOC_FREQ>::addProperty(HString name, Declared T::* memberPtr, CopyPolicy copyPolicy, PropertyFlags flags)
    {
        TProperty<Declared, T>* property = new TProperty<Declared, T>(name, memberPtr, copyPolicy, flags);
        mProperties.emplace_back(property);
        return property;
    }

    template<ObjectType T, ObjectType Base, AllocateFrequency ALLOC_FREQ>
    inline bool TClass<T, Base, ALLOC_FREQ>::isAbstract() const noexcept
    {
        return std::is_abstract_v<T>;
    }

    template <ObjectType T, ObjectType Base, AllocateFrequency ALLOC_FREQ>
    TClass<T, Base, ALLOC_FREQ>::TClass(HString name)
        : Class(name, sizeof(T), findBaseClass(), createDefaultObject())
    {
        if constexpr (!std::is_same_v<T, Object>)
            addDerivedToBase(getBaseClass(), this);
    }

    template <ObjectType T, ObjectType Base, AllocateFrequency ALLOC_FREQ>
    Class* TClass<T, Base, ALLOC_FREQ>::findBaseClass()
    {
        if constexpr (std::is_same_v<T, Object>)
            return nullptr;
        else
            return static_cast<Class*>(Reflection::getType<Base>());
    }

    template <ObjectType T, ObjectType Base, AllocateFrequency ALLOC_FREQ>
    TRef<Object> TClass<T, Base, ALLOC_FREQ>::createDefaultObject()
    {
        if constexpr (std::is_abstract_v<T>)
            return nullptr;
        else
            return createObject<T>(nullptr, ObjectFlags::Default_Object);
    }
}
