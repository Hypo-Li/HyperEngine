#include "Object.h"

namespace hyp
{
    REGISTER_CLASS(Object)

    TRef<Object> createObject(Class* clazz, Object* owner, ObjectFlags flags)
    {
        assert(!enumHasAllFlags(flags, ObjectFlags::Default_Object));
        if (clazz->isAbstract())
        {
            return nullptr;
        }
        else
        {
            Object::ConstructionContext context;
            context.owner = owner;
            context.flags = flags;
            context.guid = Guid::newGuid();
            Object* object = clazz->allocate(context.handle);
            Object::sContextStack.emplace_back(context);
            clazz->construct(object);
            Object::sContextStack.pop_back();
            clazz->registerGuid(context.guid, object);
            return object;
        }
    }

    TRef<Object> createObject(HString className, Object* owner, ObjectFlags flags)
    {
        Class* clazz = Reflection::getClass(className);
        return createObject(clazz, owner, flags);
    }

    TRef<Object> restoreObject(Class* clazz, Object* owner, Guid guid, ObjectFlags flags)
    {
        assert(!clazz->isAbstract());
        Object::ConstructionContext context;
        context.owner = owner;
        context.flags = flags;
        context.guid = guid;
        Object* object = clazz->allocate(context.handle);
        Object::sContextStack.emplace_back(context);
        clazz->construct(object);
        Object::sContextStack.pop_back();
        clazz->registerGuid(guid, object);
        return object;
    }

    void destroyObject(Object* object) noexcept
    {
        Class* clazz = object->getClass();
        ObjectHandle handle = object->getHandle();
        bool isDefaultObject = enumHasAllFlags(object->getFlags(), ObjectFlags::Default_Object);
        if (!isDefaultObject)
            clazz->unregisterGuid(object->getGuid());
        clazz->destruct(object);
        if (isDefaultObject)
        {
            std::free(object);
        }
        else
        {
            clazz->free(handle);
        }
    }

    void rebuildObject(Object* object) noexcept
    {
        Class* clazz = object->getClass();

        Object::ConstructionContext context;
        context.owner = object->getOwner();
        context.flags = object->getFlags();
        context.guid = object->getGuid();
        context.handle = object->getHandle();
        size_t refCount = object->getRefCount();

        clazz->unregisterGuid(context.guid);
        clazz->destruct(object);

        Object::sContextStack.emplace_back(context);
        clazz->construct(object);
        Object::sContextStack.pop_back();

        clazz->registerGuid(context.guid, object);

        // construct() 跑完 Refable() 的构造函数把 mRefCount 清成了 0, 这里用现成的 incRef()
        // 补回重建之前外部已经持有的那些份引用——不新增"直接设成任意值"的后门。
        for (size_t i = 0; i < refCount; ++i)
            object->incRef();
    }

    thread_local std::vector<Object::ConstructionContext> Object::sContextStack;

    Object::Object() noexcept
    {
        assert(!sContextStack.empty());
        const auto& context = sContextStack.back();
        this->mGuid = context.guid;
        this->mOwner = context.owner;
        this->mFlags = context.flags;
        this->mHandle = context.handle;
    }

    Object::~Object() noexcept
    {

    }

    const Object* Object::getRoot() const noexcept
    {
        const Object* root = this;
        while (root->mOwner)
            root = root->mOwner;
        return root;
    }

    Object* Object::getRoot() noexcept
    {
        Object* root = this;
        while (root->mOwner)
            root = root->mOwner;
        return root;
    }

    void Object::preSave()
    {
    }

    void Object::postSave()
    {
    }

    void Object::postLoad()
    {
    }

    void Object::finalizeLoad()
    {
    }

    void Object::postCopy()
    {
    }

    void Object::finalizeCopy()
    {
    }

    template <> Type* Reflection::createType<Object>()
    {
        auto c = new TClass<Object>("Object");
        return c;
    }
}
