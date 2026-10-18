#include "Entity.h"
#include "Scene.h"

#include <algorithm>

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
        // 模板 Entity(比如 Prefab::mEntities 里的)owner 是 Prefab 而不是 Scene——不能无脑
        // static_cast, 那样会把 Prefab* 硬当成 Scene* 用, 后面调用任何成员函数都是未定义行为。
        Object* owner = getOwner();
        if (!owner || !owner->getClass()->isDerivedFrom(Scene::staticClass()))
            return nullptr;
        return static_cast<Scene*>(owner);
    }

    void Entity::setName(HString newName)
    {
        // 模板 Entity 没有 Scene 可以查重名, 直接赋值——名字唯一性只在真正的场景里有意义。
        if (Scene* scene = getScene())
            scene->setEntityName(this, newName);
        else
            mName = newName;
    }

    bool Entity::setParent(Entity* newParent, ReparentMode reparentMode)
    {
        Transform* newParentTransform = newParent ? newParent->mTransform : nullptr;
        const bool changed = mTransform->setParent(newParentTransform, reparentMode);
        if (changed && newParent)
        {
            if (Scene* scene = getScene())
                scene->onEntityReparented(this);
        }
        return changed;
    }

    Entity* Entity::getParent() const noexcept
    {
        Transform* parentTransform = mTransform->getParent();
        return parentTransform ? static_cast<Entity*>(parentTransform->getOwner()) : nullptr;
    }

    void Entity::addChild(Entity* newChild, ReparentMode reparentMode)
    {
        mTransform->addChild(newChild->mTransform, reparentMode);
        if (Scene* scene = getScene())
            scene->onEntityReparented(newChild);
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

    size_t Entity::getSiblingIndex() const
    {
        if (getParent())
            return mTransform->getSiblingIndex();

        Scene* scene = getScene();
        return scene ? scene->getRootEntityIndex(this) : 0;
    }

    bool Entity::setSiblingIndex(size_t index)
    {
        if (getParent())
            return mTransform->setSiblingIndex(index);

        Scene* scene = getScene();
        return scene && scene->moveRootEntity(this, index);
    }

    Component* Entity::addComponent(Class* clazz)
    {
        assert(clazz && clazz->isDerivedFrom(Component::staticClass()));
        TRef<Object> componentObject = createObject(clazz, this);
        TRef<Component> newComponent = static_cast<Component*>(componentObject.get());
        mComponents.emplace_back(newComponent);
        if (Scene* scene = getScene())
            scene->registerComponent(newComponent);
        return newComponent;
    }

    Component* Entity::addComponent(HString className)
    {
        Class* clazz = Reflection::getClass(className);
        if (!clazz)
            return nullptr;
        return addComponent(clazz);
    }

    void Entity::attachComponent(Component* component)
    {
        mComponents.emplace_back(component);
        if (Scene* scene = getScene())
            scene->registerComponent(component);
    }

    Component* Entity::getComponent(Class* clazz, Guid guid) const
    {
        for (const TRef<Component>& component : mComponents)
        {
            if (!component->getClass()->isDerivedFrom(clazz))
                continue;
            if (!guid.isValid() || component->getGuid() == guid)
                return component.get();
        }
        return nullptr;
    }

    bool Entity::removeComponent(Component* component)
    {
        auto it = std::find(mComponents.begin(), mComponents.end(), component);
        if (it != mComponents.end())
        {
            if (Scene* scene = getScene())
                scene->unregisterComponent(component);
            mComponents.erase(it);
            return true;
        }
        return false;
    }

    size_t Entity::getComponentIndex(const Component* component) const
    {
        auto it = std::find_if(mComponents.begin(), mComponents.end(), [component](const TRef<Component>& ref) { return ref.get() == component; });
        return it != mComponents.end() ? static_cast<size_t>(it - mComponents.begin()) : Scene::kInvalidIndex;
    }

    bool Entity::moveComponent(Component* component, size_t index)
    {
        auto it = std::find_if(mComponents.begin(), mComponents.end(), [component](const TRef<Component>& ref) { return ref.get() == component; });
        if (it == mComponents.end())
            return false;

        TRef<Component> self = std::move(*it);
        mComponents.erase(it);
        mComponents.insert(mComponents.begin() + std::min(index, mComponents.size()), std::move(self));
        return true;
    }

    void Entity::removeAllComponents()
    {
        Scene* scene = getScene();
        for (auto& component : mComponents)
        {
            if (scene)
                scene->unregisterComponent(component);
        }
        mComponents.clear();
    }

    void Entity::setMobility(Mobility mobility)
    {
        if (mMobility == mobility)
            return;

        mMobility = mobility;
        for (TRef<Component>& component : mComponents)
            component->onMobilityChanged(mobility);
    }

    void Entity::setVisible(bool visible)
    {
        if (mVisible == visible)
            return;

        mVisible = visible;
        for (TRef<Component>& component : mComponents)
        {
            if (component->getState() == Component::State::Registered)
                component->onVisibilityChanged(visible);
        }
    }

    void Entity::finalizeCopy()
    {
        // Copier 只负责重建对象图(Transform/Components 的数据本身), 不知道 Scene 的存在——
        // 复制出来的这份数据要变成"活的"(进 mEntities、组件被送进注册流程), 需要自己在这里
        // 补上。newOwner 不是 Scene 的情况也是合法的(比如把一个运行时 Entity 拷贝进 Prefab
        // 模板, newOwner 传的是 Prefab), 这份拷贝本来就不该进任何 Scene, 直接跳过。
        if (Scene* scene = getScene())
            scene->registerEntity(this);
    }

    void Entity::finalizeLoad()
    {
        // 跟 finalizeCopy() 要补的东西不一样: mEntities/mExistingNames 那一半是 Scene 自己
        // 的反射属性/收尾工作(见 Scene::finalizeLoad()), 这个 Entity 已经天然是 mEntities
        // 里的一个元素, 不需要(也不该)再调 Scene::registerEntity() 那整个方法, 否则会被
        // 重复塞进 mEntities。这里只补组件注册这一半——mState 不是反射属性, 反序列化出来的
        // Component 停在默认的 Initial, 需要送进 Scene 的注册队列才会被 tick/onRegister()。
        // 放在 finalizeLoad(而不是更早的 postLoad)是因为 mPendingRegisterComponents 是
        // Scene 的共享状态, 按照"postLoad 只保证自己这份数据有效、不碰共享状态"的约定,
        // 要等整棵树都读完、进入 finalizeLoad 阶段才能碰。owner 不是 Scene 的情况(比如这是
        // 从磁盘反序列化出来的 Prefab 模板 Entity)本来就不需要注册, 跳过。
        if (Scene* scene = getScene())
        {
            for (const auto& component : mComponents)
                scene->registerComponent(component.get());
        }
    }

    template <> Type* Reflection::createType<Mobility>()
    {
        auto e = new TEnumeration<Mobility>("Mobility");
        e->addValue("Static", Mobility::Static);
        e->addValue("Dynamic", Mobility::Dynamic);
        return e;
    }

    template <> Type* Reflection::createType<Entity>()
    {
        auto c = new TClass<Entity>("Entity");
        c->addProperty("Name", &Entity::mName);
        c->addProperty("Transform", &Entity::mTransform, CopyPolicy::Deep);
        c->addProperty("Components", &Entity::mComponents, CopyPolicy::Deep);
        c->addProperty("Mobility", &Entity::mMobility);
        c->addProperty("Visible", &Entity::mVisible);

        return c;
    }
}
