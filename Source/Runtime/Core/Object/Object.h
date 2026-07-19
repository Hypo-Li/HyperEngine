#pragma once
#include "../Common/Refable.h"
#include "../Common/EnumBitwise.h"
#include "../Common/Guid.h"
#include "../Common/LogSystem.h"
#include "ObjectDefines.h"

#define REFLECT_CLASS(ClassName) \
    friend class Reflection; \
    static Class* sClass; \
public: \
    static Class* staticClass(); \
    virtual Class* getClass() const;

#define REGISTER_CLASS(ClassName) \
    Class* ClassName::sClass = Reflection::getClass<ClassName>(); \
    Class* ClassName::staticClass() { return sClass ? sClass : Reflection::getClass<ClassName>(); } \
    Class* ClassName::getClass() const { return staticClass(); }

namespace hyp
{
    class Object;

    template <ObjectType T>
    TRef<T> createObject(Object* owner, ObjectFlags flags);

    TRef<Object> createObject(Class* clazz, Object* owner, ObjectFlags flags = ObjectFlags::None);

    TRef<Object> createObject(HString className, Object* owner, ObjectFlags flags = ObjectFlags::None);

    // Only for reconstructing a previously-serialized Object during deserialization,
    // where the guid must match what was originally exported. Not for general use.
    TRef<Object> restoreObject(Class* clazz, Object* owner, Guid guid, ObjectFlags flags = ObjectFlags::Load);

    void destroyObject(Object* object) noexcept;

    class Object : public Refable
    {
        template <ObjectType T>
        friend TRef<T> createObject(Object* owner, ObjectFlags flags);

        friend TRef<Object> createObject(Class* clazz, Object* owner, ObjectFlags flags);

        friend TRef<Object> restoreObject(Class* clazz, Object* owner, Guid guid, ObjectFlags flags);

        friend void destroyObject(Object* object) noexcept;

        REFLECT_CLASS(Object)
    public:
        Object() noexcept;

        Object(const Object&) = delete;

        virtual ~Object() noexcept;

        Object& operator=(const Object&) = delete;

        Guid getGuid() const noexcept
        {
            return mGuid;
        }

        Object* getOwner() const noexcept
        {
            return mOwner;
        }

        const Object* getRoot() const noexcept;

        Object* getRoot() noexcept;

        ObjectFlags getFlags() const noexcept
        {
            return mFlags;
        }

        ObjectHandle getHandle() const noexcept
        {
            return mHandle;
        }

        virtual void preSave();

        virtual void postSave();

        virtual void postLoad();

        virtual void finalizeLoad();

        virtual void postCopy();

        virtual void finalizeCopy();

    private:
        Guid mGuid;
        Object* mOwner;
        ObjectFlags mFlags;
        ObjectHandle mHandle;

        struct ConstructionContext
        {
            Object* owner;
            ObjectFlags flags;
            ObjectHandle handle;
            Guid guid;
        };
        static thread_local std::vector<ConstructionContext> sContextStack;
    };

    template <ObjectType T>
    TRef<T> createObject(Object* owner, ObjectFlags flags = ObjectFlags::None)
    {
        if constexpr (std::is_abstract_v<T>)
        {
            return nullptr;
        }
        else
        {
            Object::ConstructionContext context;
            context.owner = owner;
            context.flags = flags;
            context.guid = Guid::newGuid();
            T* object = nullptr;
            if (enumHasAllFlags(flags, ObjectFlags::Default_Object))
            {
                object = static_cast<T*>(std::malloc(sizeof(T)));
            }
            else
            {
                Class* clazz = T::staticClass();
                object = static_cast<T*>(clazz->allocate(context.handle));
            }
            Object::sContextStack.emplace_back(context);
            std::construct_at(object);
            Object::sContextStack.pop_back();
            return object;
        }
    }

    template <> Type* Reflection::createType<Object>();
}
