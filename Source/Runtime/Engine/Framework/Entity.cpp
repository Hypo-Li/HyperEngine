#include "Entity.h"
#include "Scene.h"

namespace hyp
{
    REGISTER_CLASS(Entity)

    Entity::Entity() noexcept
    {
        if (!enumHasAllFlags(getFlags(), ObjectFlags::Default_Object))
        {
            mTransform = createObject<Transform>(this);
        }
    }

    Entity::~Entity() noexcept
    {
    }

    Scene* Entity::getScene() const noexcept
    {
        return static_cast<Scene*>(getOwner());
    }

    void Entity::setName(HString newName)
    {
        getScene()->setEntityName(this, newName);
    }

    bool Entity::setParent(Entity* newParent, ReparentMode reparentMode)
    {
        Transform* newParentTransform = newParent ? newParent->mTransform : nullptr;
        return mTransform->setParent(newParentTransform, reparentMode);
    }

    Entity* Entity::getParent() const noexcept
    {
        Transform* parentTransform = mTransform->getParent();
        return parentTransform ? static_cast<Entity*>(parentTransform->getOwner()) : nullptr;
    }

    void Entity::addChild(Entity* newChild, ReparentMode reparentMode)
    {
        mTransform->addChild(newChild->mTransform, reparentMode);
    }

    size_t Entity::getChildCount() const noexcept
    {
        return mTransform->getChildCount();
    }

    Entity* Entity::getChild(size_t index) const noexcept
    {
        Transform* childTransform = mTransform->getChild(index);
        return childTransform ? static_cast<Entity*>(childTransform->getOwner()) : nullptr;
    }

    bool Entity::removeChild(Entity* child, ReparentMode reparentMode)
    {
        return mTransform->removeChild(child->mTransform, reparentMode);
    }

    void Entity::removeChildren(size_t startIndex, size_t count, ReparentMode reparentMode)
    {
        mTransform->removeChildren(startIndex, count, reparentMode);
    }

    Component* Entity::addComponent(Class* clazz)
    {
        assert(clazz && clazz->isDerivedFrom(Component::staticClass()));
        TRef<Object> componentObject = createObject(clazz, this);
        TRef<Component> newComponent = static_cast<Component*>(componentObject.get());
        mComponents.emplace_back(newComponent);
        getScene()->registerComponent(newComponent);
        return newComponent;
    }

    Component* Entity::addComponent(HString className)
    {
        Class* clazz = Reflection::getClass(className);
        if (!clazz)
            return nullptr;
        return addComponent(clazz);
    }

    bool Entity::removeComponent(Component* component)
    {
        auto it = std::find(mComponents.begin(), mComponents.end(), component);
        if (it != mComponents.end())
        {
            getScene()->unregisterComponent(component);
            mComponents.erase(it);
            return true;
        }
        return false;
    }

    void Entity::removeAllComponents()
    {
        for (auto& component : mComponents)
        {
            getScene()->unregisterComponent(component);
        }
        mComponents.clear();
    }

    template <> Type* Reflection::createType<Entity>()
    {
        auto c = new TClass<Entity>("Entity");
        c->addProperty("Name", &Entity::mName);
        c->addProperty("Transform", &Entity::mTransform, CopyPolicy::Deep);
        c->addProperty("Components", &Entity::mComponents, CopyPolicy::Deep);

        return c;
    }
}
