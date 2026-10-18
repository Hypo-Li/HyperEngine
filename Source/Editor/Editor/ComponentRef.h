#pragma once
#include <Engine/Framework/Component.h>
#include <Engine/Framework/Entity.h>
#include <Engine/Framework/Prefab.h>
#include <Engine/Framework/Scene.h>

#include <cassert>

namespace hyp::ed
{
    // undo/redo 的闭包要活到很久以后才可能被调用, 这期间对应的 Entity 完全可能已经被删除、
    // 又被 undo 恢复成新地址的对象——不能捕获 Entity*, 只能捕获稳定身份(容器 Guid + Entity
    // Guid), 每次真正要用的时候现查一遍。容器也按 guid 现查, 不直接存指针——Play/Stop 会把
    // 当前场景整个销毁重建(同一个 guid、新的对象地址), 捕获的 Scene*/Prefab* 到那时候就是
    // 悬空指针; Scene/Prefab 各自的 Class::staticClass() 都维护着自己的 GuidRegistry, 随
    // createObject()/restoreObject()/destroyObject() 自动更新, 用 findByGuid() 查到的永远
    // 是这个 guid 当前对应的活对象。
    //
    // Entity 的容器不止 Scene 一种——Prefab 模板里的 Entity, owner 是 Prefab 不是 Scene
    // (Entity::getScene() 对这种情况返回空), 但 Prefab 同样是按 guid 全局可查的 Object,
    // Prefab::findNode() 又正好是"按 guid 查树里的 Entity/Component"这件事在 Prefab 语境下
    // 的对应物, 跟 Scene::findEntity() 是同一个角色——用同一个 EntityRef 类型按
    // containerIsPrefab 分两条路解析, 面板代码完全不需要关心自己在哪种容器里画。
    struct EntityRef
    {
        Guid containerGuid;
        bool containerIsPrefab = false;
        Guid entityGuid;

        static EntityRef capture(Entity& entity)
        {
            if (Scene* scene = entity.getScene())
                return EntityRef{ scene->getGuid(), false, entity.getGuid() };

            Prefab* prefab = static_cast<Prefab*>(entity.getOwner());
            return EntityRef{ prefab->getGuid(), true, entity.getGuid() };
        }

        Entity* resolve() const
        {
            if (containerIsPrefab)
            {
                Prefab* prefab = static_cast<Prefab*>(Prefab::staticClass()->findByGuid(containerGuid));
                assert(prefab);
                Entity* entity = static_cast<Entity*>(prefab->findNode(entityGuid));
                assert(entity);
                return entity;
            }

            Scene* scene = static_cast<Scene*>(Scene::staticClass()->findByGuid(containerGuid));
            assert(scene);
            Entity* entity = scene->findEntity(entityGuid);
            assert(entity);
            return entity;
        }
    };

    // 每个面板的每条 undo 命令都要重复"现查 Entity 再拿 Component"这几行, 这个类把"怎么查"
    // 收在一处, 调用方只捕获一个 ComponentRef 就够了。
    struct ComponentRef
    {
        EntityRef entityRef;
        Class* componentClass = nullptr;
        Guid componentGuid;

        static ComponentRef capture(Component& component)
        {
            return ComponentRef{ EntityRef::capture(*component.getEntity()), component.getClass(), component.getGuid() };
        }

        Entity* resolveEntity() const
        {
            return entityRef.resolve();
        }

        Component* resolve() const
        {
            return resolveEntity()->getComponent(componentClass, componentGuid);
        }

        template <typename T> requires std::is_base_of_v<Component, T>
        T* resolveAs() const
        {
            return static_cast<T*>(resolve());
        }
    };
}
