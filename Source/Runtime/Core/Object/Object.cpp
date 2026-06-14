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
            Object* object = clazz->allocate(context.handle);
            Object::sContextStack.emplace_back(context);
            clazz->construct(object);
            Object::sContextStack.pop_back();
            return object;
        }
    }

    TRef<Object> createObject(HString className, Object* owner, ObjectFlags flags)
    {
        Class* clazz = Reflection::getClass(className);
        return createObject(clazz, owner, flags);
    }

    void destroyObject(Object* object) noexcept
    {
        Class* clazz = object->getClass();
        ObjectHandle handle = object->getHandle();
        clazz->destruct(object);
        if (enumHasAllFlags(object->getFlags(), ObjectFlags::Default_Object))
        {
            std::free(object);
        }
        else
        {
            clazz->free(handle);
        }
    }

    thread_local std::vector<Object::ConstructionContext> Object::sContextStack;

    Object::Object() noexcept
        : mGuid(Guid::newGuid())
    {
        assert(!sContextStack.empty());
        const auto& context = sContextStack.back();
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
