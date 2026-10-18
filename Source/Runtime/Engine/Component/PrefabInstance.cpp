#include "PrefabInstance.h"

#include "../Framework/Scene.h"

#include <Core/Serialization/CopyContext.h>
#include <Core/Serialization/Copier.h>

#include <algorithm>
#include <cassert>

namespace hyp
{
    namespace
    {
        // Class::getProperties() 只返回这一层自己声明的属性, 不含基类——同步要覆盖一个节点
        // 全部的反射属性(包括从 Component/Object 继承来的), 这里跟 Copier::collectAllProperties
        // 是同一个逻辑, 因为那份是私有的、不对外开放, 这里单独写一份, 基类优先、跟 Serializer/
        // Copier 保持一致的顺序。
        void collectAllProperties(Class* clazz, std::vector<Property*>& outProperties)
        {
            if (clazz->getBaseClass())
                collectAllProperties(clazz->getBaseClass(), outProperties);
            for (Property* property : clazz->getProperties())
                outProperties.emplace_back(property);
        }
    }

    REGISTER_CLASS(PrefabInstance)

    void PrefabInstance::onRegister()
    {
        if (!mSourcePrefab)
            return;

        mSourcePrefab->addLiveInstance(this);
        updateFromPrefab();
    }

    void PrefabInstance::onUnregister()
    {
        if (mSourcePrefab)
            mSourcePrefab->removeLiveInstance(this);
    }

    void PrefabInstance::mapNode(Guid templateNodeId, Object* runtimeNode)
    {
        mNodeMap[templateNodeId] = runtimeNode;
        // 映射和"已排除"互斥——一个节点只要被映射了, 就不该再算作 removeNode() 排除过的。
        std::erase(mRemovedNodes, templateNodeId);
    }

    Object* PrefabInstance::resolveNode(Guid templateNodeId) const
    {
        auto it = mNodeMap.find(templateNodeId);
        return it != mNodeMap.end() ? it->second : nullptr;
    }

    std::optional<Guid> PrefabInstance::findTemplateNodeId(Object* node) const
    {
        for (const auto& [templateNodeId, runtimeNode] : mNodeMap)
        {
            if (runtimeNode == node)
                return templateNodeId;
        }
        return std::nullopt;
    }

    void PrefabInstance::addOverride(Guid templateNodeId, HString propertyName)
    {
        if (isOverridden(templateNodeId, propertyName))
            return;
        mOverrides.emplace_back(Override{ templateNodeId, propertyName });
    }

    void PrefabInstance::removeOverride(Guid templateNodeId, HString propertyName)
    {
        auto it = std::find_if(
            mOverrides.begin(),
            mOverrides.end(),
            [&](const Override& o) { return o.templateNodeId == templateNodeId && o.propertyName == propertyName; }
        );
        if (it != mOverrides.end())
            mOverrides.erase(it);
    }

    bool PrefabInstance::isOverridden(Guid templateNodeId, HString propertyName) const
    {
        return std::any_of(
            mOverrides.begin(),
            mOverrides.end(),
            [&](const Override& o) { return o.templateNodeId == templateNodeId && o.propertyName == propertyName; }
        );
    }

    void PrefabInstance::markNodeRemoved(Guid templateNodeId)
    {
        if (isNodeRemoved(templateNodeId))
            return;
        mRemovedNodes.emplace_back(templateNodeId);
    }

    bool PrefabInstance::isNodeRemoved(Guid templateNodeId) const
    {
        return std::find(mRemovedNodes.begin(), mRemovedNodes.end(), templateNodeId) != mRemovedNodes.end();
    }

    Object* PrefabInstance::removeNode(Guid templateNodeId)
    {
        Object* runtimeNode = resolveNode(templateNodeId);
        if (!runtimeNode)
            return nullptr;

        mNodeMap.erase(templateNodeId);
        markNodeRemoved(templateNodeId);
        return runtimeNode;
    }

    bool PrefabInstance::isConnectedToInstanceRoot(Entity* entity) const
    {
        Entity* instanceRoot = getEntity();
        for (Entity* current = entity; current; current = current->getParent())
        {
            if (current == instanceRoot)
                return true;
        }
        return false;
    }

    bool PrefabInstance::markNodeAdded(Object* node)
    {
        bool isComponentNode = node->getClass()->isDerivedFrom(Component::staticClass());
        Entity* entity = isComponentNode ? static_cast<Component*>(node)->getEntity() : static_cast<Entity*>(node);

        if (!isConnectedToInstanceRoot(entity))
            return false;

        if (!isNodeAdded(node))
            mAddedNodes.emplace_back(node);
        return true;
    }

    void PrefabInstance::unmarkNodeAdded(Object* node)
    {
        auto it = std::find(mAddedNodes.begin(), mAddedNodes.end(), node);
        if (it != mAddedNodes.end())
            mAddedNodes.erase(it);
    }

    bool PrefabInstance::isNodeAdded(Object* node) const
    {
        return std::find(mAddedNodes.begin(), mAddedNodes.end(), node) != mAddedNodes.end();
    }

    void PrefabInstance::removeStaleNodesFromPrefab()
    {
        std::vector<Guid> templateDeleted;
        std::vector<Guid> disconnected;

        Guid rootGuid = mSourcePrefab->getRootEntity()->getGuid();

        for (const auto& [guid, runtimeNode] : mNodeMap)
        {
            Object* templateNode = mSourcePrefab->findNode(guid);
            if (!templateNode)
            {
                templateDeleted.emplace_back(guid);
                continue;
            }

            // 只有 Entity 才有独立的 reparent 概念, Component 只能被显式 attach/remove;
            // 根节点自己挂在场景里哪个位置不受这份实例子树的约束, 两种情况都不检查连通性。
            if (guid == rootGuid || !templateNode->getClass()->isDerivedFrom(Entity::staticClass()))
                continue;

            if (!isConnectedToInstanceRoot(static_cast<Entity*>(runtimeNode)))
                disconnected.emplace_back(guid);
        }

        for (Guid guid : templateDeleted)
        {
            Object* runtimeNode = resolveNode(guid);

            if (runtimeNode->getClass()->isDerivedFrom(Entity::staticClass()))
            {
                // destroyChildren 传 false: 挂在它底下的子 Entity 如果本身没被删, 不该被
                // 连带错杀——Scene::destroyEntity() 会把它们原地重新挂到这个 Entity 的父
                // 节点上, 它们是不是真的也该消失, 交给这个函数自己这轮扫描独立判断。
                getScene()->destroyEntity(static_cast<Entity*>(runtimeNode), false);
            }
            else
            {
                Component* component = static_cast<Component*>(runtimeNode);
                component->getEntity()->removeComponent(component);
            }

            mNodeMap.erase(guid);
        }

        for (Guid guid : disconnected)
        {
            mNodeMap.erase(guid);
            markNodeRemoved(guid);
        }
    }

    void PrefabInstance::updateFromPrefab()
    {
        if (!mSourcePrefab)
            return;
        if (mSyncedVersion == mSourcePrefab->getVersion())
            return;

        removeStaleNodesFromPrefab();

        std::vector<Object*> runtimeNodes;

        // 第一遍: 每个节点写完属性就立即调 postLoad()——postLoad() 只保证"这个节点自己的
        // 数据有效", 不依赖别的节点是不是也写完了, 这跟 Serializer.cpp 里 objects[i]->
        // postLoad() 紧跟在那一个对象自己属性读完之后立即调是同一个语义(见
        // Serializer.cpp:585-589), 不能写成"全部节点写完属性再统一 postLoad"这种看起来
        // 像是在等整张图的批量形式, 那是 finalizeLoad() 的语义, 不是 postLoad() 的。
        for (const TRef<Entity>& templateEntity : mSourcePrefab->getEntities())
        {
            Object* runtimeEntityNode = resolveNode(templateEntity->getGuid());
            if (!runtimeEntityNode)
                continue;

            writePropertiesFromTemplate(templateEntity.get(), runtimeEntityNode);
            runtimeEntityNode->postLoad();
            runtimeNodes.emplace_back(runtimeEntityNode);

            // Transform 不在 mNodeMap 里(§3.1), 不走"新增/删除节点"那条结构性路径, 只在这里
            // 当成 Entity 的附属节点同步值——Local Position/Rotation/Scale/Socket Name 是
            // 普通 Shallow 值属性, 直接复用泛型循环; Parent 是指向另一个模板节点的类指针,
            // 泛型循环里已经跳过了它, 下面单独处理重定向。
            //
            // 实例根节点的整个 Transform(Position/Rotation/Scale/Socket Name/Parent)完全
            // 不跟模板走——根节点"挂在场景里哪个位置"是运行时自己的事(J.40), 不只是"挂在
            // 谁下面"这一项: 模板里根节点的坐标是"以自己为原点"这个没有实际意义的数, 用户
            // 把实例拖到场景里任何位置之后, 它自己的 local 坐标就已经不是模板那个数能代表的
            // 东西了, 同步了会把用户摆好的位置/朝向/缩放强行拽回去。
            if (templateEntity.get() != mSourcePrefab->getRootEntity())
            {
                Entity* runtimeEntity = static_cast<Entity*>(runtimeEntityNode);
                Transform& runtimeTransform = runtimeEntity->getTransform();
                Transform& templateTransform = templateEntity->getTransform();
                writePropertiesFromTemplate(&templateTransform, &runtimeTransform);
                runtimeTransform.postLoad();

                if (!isOverridden(templateTransform.getGuid(), "Parent"_hs))
                {
                    Transform* templateParent = templateTransform.getParent();
                    Transform* runtimeParent = nullptr;
                    if (templateParent)
                    {
                        Object* runtimeParentEntity = resolveNode(static_cast<Entity*>(templateParent->getOwner())->getGuid());
                        if (runtimeParentEntity)
                            runtimeParent = &static_cast<Entity*>(runtimeParentEntity)->getTransform();
                    }
                    // Keep_Local: 这是模板结构变化, 不是运行时"用户在视口里拖了一下"那种需要
                    // 保持世界位置的操作(B.10)。已经指向同一个 parent 就跳过, 不做无意义调用。
                    if (runtimeTransform.getParent() != runtimeParent)
                        runtimeTransform.setParent(runtimeParent, ReparentMode::Keep_Local);
                }
            }

            for (const TRef<Component>& templateComponent : templateEntity->getAllComponents())
            {
                Object* runtimeComponentNode = resolveNode(templateComponent->getGuid());
                if (!runtimeComponentNode)
                    continue;

                // 写属性之前先 unregister_(): 如果这个 Component 的 onUnregister() 要按
                // 当前(即将被覆盖的旧)属性值去清理 onRegister() 期间建立的状态(比如按旧
                // 半径把自己从某个空间索引里摘掉), 属性已经被新值覆盖再调就会清理错。
                Component* component = static_cast<Component*>(runtimeComponentNode);
                assert(component != this && "PrefabInstance 不是模板节点, 不应该出现在 mNodeMap 里");
                component->doUnregister();

                // rebuildObject() 把这个 Component 析构再重新构造, 让 postLoad()/finalizeLoad()
                // 在一个真正"刚构造"的对象上跑, 而不是在一个已经活过一段时间的对象上打补丁——
                // 地址、guid、引用计数都保持不变, 外部(比如 mNodeMap 自己, 或者其它节点的
                // Shallow 指针)持有的裸指针不会失效。
                auto savedOverrides = captureOverriddenValues(templateComponent->getGuid(), component);
                rebuildObject(component);
                writePropertiesFromTemplate(templateComponent.get(), runtimeComponentNode);
                restoreOverriddenValues(component, savedOverrides);

                runtimeComponentNode->postLoad();
                runtimeNodes.emplace_back(runtimeComponentNode);
            }
        }

        // 第二遍: 整个 Prefab 范围内所有节点都过完第一遍之后, 才统一调 finalizeLoad()——
        // 对应 Serializer::serializeObject() 里 serializeClass() 全部跑完(唯一入口对应的
        // 整棵树都读完)之后才调 mContext.finalizeLoad() 那个顺序(见 Serializer.cpp:11-19、
        // SerializeContext.cpp:133-141)。Entity::finalizeLoad() 会把自己名下每个 Component
        // 重新送进 Scene::registerComponent() 的排队机制(它们被 rebuildObject() 打回
        // Initial 之后就是这个状态该走的正常路径)——不需要、也不能在这里再手动调一次
        // Component::doRegister(): Scene::mPendingRegisterComponents 里已经有了这条排队
        // 记录, 手动注册只会让下一次 Scene::tick() 的 processPending() 对着一个已经
        // Registered 的组件重复调用 doRegister(), 触发 assert。跟正常的反序列化加载、
        // undo/redo 恢复走的是同一条路: onRegister() 生效推迟到下一帧, 不是立即同步生效。
        for (Object* node : runtimeNodes)
            node->finalizeLoad();

        instantiateNewNodesFromPrefab();

        mSyncedVersion = mSourcePrefab->getVersion();
    }

    void PrefabInstance::instantiateNewNodesFromPrefab()
    {
        // 先只判断有没有新节点(全靠 resolveNode() 查表, 很便宜), 完全没有的话就不用往下
        // 建 CopyContext、也不用付 mSourcePrefab->findNode() 那个线性扫描的成本——这是
        // updateFromPrefab() 每次都会走到的路径, 大多数情况下模板没有结构性变化。
        std::vector<Entity*> newEntities;
        std::vector<std::pair<Entity*, Component*>> newComponents;

        for (const TRef<Entity>& templateEntity : mSourcePrefab->getEntities())
        {
            Guid entityGuid = templateEntity->getGuid();
            Object* runtimeEntityNode = resolveNode(entityGuid);
            if (!runtimeEntityNode)
            {
                // 查不到不一定是"模板新增"——也可能是这份实例自己删过, 那种情况不该被
                // 复活, markNodeRemoved() 记录的就是这个意图。
                if (!isNodeRemoved(entityGuid))
                    newEntities.emplace_back(templateEntity.get());
                continue;
            }

            for (const TRef<Component>& templateComponent : templateEntity->getAllComponents())
            {
                Guid componentGuid = templateComponent->getGuid();
                if (!resolveNode(componentGuid) && !isNodeRemoved(componentGuid))
                    newComponents.emplace_back(static_cast<Entity*>(runtimeEntityNode), templateComponent.get());
            }
        }

        if (newEntities.empty() && newComponents.empty())
            return;

        CopyContext context;
        Copier copier(context);

        // 把已经登记过的映射喂给这份 CopyContext——新节点如果通过 Shallow 引用(比如
        // Transform::mParent)指向某个已存在的节点, populate 阶段才能查到、重定向到运行时
        // 对象, 而不是退化成指向模板。mNodeMap 本身不含 Transform(3.1 还没做), 这里额外
        // 按 Entity 补一条 Transform 的映射。
        for (const auto& [guid, runtimeNode] : mNodeMap)
        {
            Object* templateNode = mSourcePrefab->findNode(guid);
            if (!templateNode)
                continue;

            context.registerExternalCopy(templateNode, runtimeNode);

            if (templateNode->getClass()->isDerivedFrom(Entity::staticClass()))
            {
                Entity* templateEntity = static_cast<Entity*>(templateNode);
                Entity* runtimeEntity = static_cast<Entity*>(runtimeNode);
                context.registerExternalCopy(&templateEntity->getTransform(), &runtimeEntity->getTransform());
            }
        }

        Scene* scene = getScene();
        for (Entity* templateEntity : newEntities)
            copier.copy(templateEntity, scene);
        for (const auto& [runtimeEntity, templateComponent] : newComponents)
            copier.copy(templateComponent, runtimeEntity);

        copier.finalizeAll();

        for (Entity* templateEntity : newEntities)
        {
            mapNode(templateEntity->getGuid(), context.findCopy(templateEntity));
            for (const TRef<Component>& templateComponent : templateEntity->getAllComponents())
                mapNode(templateComponent->getGuid(), context.findCopy(templateComponent.get()));
        }

        for (const auto& [runtimeEntity, templateComponent] : newComponents)
        {
            Component* newComponent = static_cast<Component*>(context.findCopy(templateComponent));
            mapNode(templateComponent->getGuid(), newComponent);
            runtimeEntity->attachComponent(newComponent);
        }
    }

    void PrefabInstance::writePropertiesFromTemplate(Object* templateNode, Object* runtimeNode)
    {
        // 只会被拿 Entity、Entity 身上的 Component、或者 Entity::mTransform 调用。
        bool isComponentNode = runtimeNode->getClass()->isDerivedFrom(Component::staticClass());
        bool isTransformNode = runtimeNode->getClass()->isDerivedFrom(Transform::staticClass());

        std::vector<Property*> properties;
        collectAllProperties(templateNode->getClass(), properties);
        for (Property* property : properties)
        {
            // Deep/Ignore 是结构性槽位(Transform/Components/Children 这类), 由新增/删除
            // 节点那条路径处理, 不归这里管。
            if (property->getCopyPolicy() != CopyPolicy::Shallow)
                continue;
            if (isOverridden(templateNode->getGuid(), property->getName()))
                continue;
            // Entity::mName 是实例自己的场景内身份, 不该跟着模板走——这是产品层面的决定
            // (每个实例的名字本来就该各自独立), 不是缺了通用机制的权宜之计。
            if (!isComponentNode && !isTransformNode && property->getName() == "Name"_hs)
                continue;
            // Transform::mParent 是指向另一个模板节点的 Shallow 类指针, 原样抄值会指到模板
            // 对象上——同步时要重定向到运行时对应节点, 而且实例根节点的 Parent 完全不能跟
            // 模板走(根节点在场景里挂在哪是运行时自己的事), 两种情况都不是这个泛型循环能
            // 处理的, 在 updateFromPrefab() 里单独处理。
            if (isTransformNode && property->getName() == "Parent"_hs)
                continue;

            property->setRawValue(runtimeNode, property->getValuePtr(templateNode));
        }
    }

    std::vector<std::pair<Property*, void*>> PrefabInstance::captureOverriddenValues(Guid templateNodeId, Object* runtimeNode) const
    {
        std::vector<std::pair<Property*, void*>> saved;
        for (const Override& o : mOverrides)
        {
            if (o.templateNodeId != templateNodeId)
                continue;

            Property* property = runtimeNode->getClass()->getProperty(o.propertyName);
            if (!property)
                continue;

            Type* type = property->getDeclaredType();
            void* scratch = ::operator new(type->getSize());
            type->construct(scratch);
            property->getValue(runtimeNode, scratch);
            saved.emplace_back(property, scratch);
        }
        return saved;
    }

    void PrefabInstance::restoreOverriddenValues(Object* runtimeNode, std::vector<std::pair<Property*, void*>>& savedValues)
    {
        for (auto& [property, scratch] : savedValues)
        {
            property->setRawValue(runtimeNode, scratch);
            property->getDeclaredType()->destruct(scratch);
            ::operator delete(scratch);
        }
        savedValues.clear();
    }

    void PrefabInstance::applyToPrefab()
    {
        if (!mSourcePrefab)
            return;

        for (const Override& o : mOverrides)
        {
            Object* runtimeNode = resolveNode(o.templateNodeId);
            Object* templateNode = mSourcePrefab->findNode(o.templateNodeId);
            if (!runtimeNode || !templateNode)
                continue;

            // 模板节点从没注册进任何 Scene, setName()/setMobility() 那些副作用不适用,
            // 直接反射赋值就是安全的, 不需要跟 writePropertiesFromTemplate() 一样特判。
            Property* property = runtimeNode->getClass()->getProperty(o.propertyName);
            if (!property)
                continue;

            property->setRawValue(templateNode, property->getValuePtr(runtimeNode));
        }

        mOverrides.clear();

        promoteAddedNodesToPrefab();

        mSourcePrefab->apply();
    }

    void PrefabInstance::promoteAddedNodesToPrefab()
    {
        if (mAddedNodes.empty())
            return;

        std::vector<Entity*> entitiesToPromote;
        std::vector<Component*> componentsToPromote;
        for (Object* node : mAddedNodes)
        {
            if (node->getClass()->isDerivedFrom(Entity::staticClass()))
                entitiesToPromote.emplace_back(static_cast<Entity*>(node));
            else
                componentsToPromote.emplace_back(static_cast<Component*>(node));
        }

        CopyContext context;
        Copier copier(context);

        // 跟 instantiateNewNodesFromPrefab() 同样的道理, 只是方向相反: 这次拷贝的是运行时
        // 对象, 它们的 Shallow 引用(比如 Transform::mParent)如果指向已经追踪的节点, 要能
        // 查到重定向到模板侧的副本, 而不是退化成指向运行时对象。
        for (const auto& [guid, runtimeNode] : mNodeMap)
        {
            Object* templateNode = mSourcePrefab->findNode(guid);
            if (!templateNode)
                continue;

            context.registerExternalCopy(runtimeNode, templateNode);

            if (templateNode->getClass()->isDerivedFrom(Entity::staticClass()))
            {
                Entity* runtimeEntity = static_cast<Entity*>(runtimeNode);
                Entity* templateEntity = static_cast<Entity*>(templateNode);
                context.registerExternalCopy(&runtimeEntity->getTransform(), &templateEntity->getTransform());
            }
        }

        for (Entity* entity : entitiesToPromote)
            copier.copy(entity, mSourcePrefab.get());

        // 宿主 Entity 也在这批要提升的实体里的 Component, 不用单独拷贝——会随 Entity 的 Deep
        // 拷贝自动带过去; 只有宿主已经是模板里现成的追踪节点时才需要单独处理。宿主既不在这批
        // 里、自己也还没被追踪的(还没轮到它被提升), 这次先跳过, 留在 mAddedNodes 里。
        std::vector<std::pair<Component*, Entity*>> individualComponents;
        for (Component* component : componentsToPromote)
        {
            Entity* ownerEntity = component->getEntity();
            if (std::find(entitiesToPromote.begin(), entitiesToPromote.end(), ownerEntity) != entitiesToPromote.end())
                continue;

            std::optional<Guid> ownerTemplateId = findTemplateNodeId(ownerEntity);
            if (!ownerTemplateId)
                continue;

            Entity* templateOwnerEntity = static_cast<Entity*>(mSourcePrefab->findNode(*ownerTemplateId));
            copier.copy(component, templateOwnerEntity);
            individualComponents.emplace_back(component, templateOwnerEntity);
        }

        copier.finalizeAll();

        for (Entity* entity : entitiesToPromote)
        {
            Entity* templateEntity = static_cast<Entity*>(context.findCopy(entity));
            mSourcePrefab->mEntities.emplace_back(templateEntity);
            mapNode(entity->getGuid(), entity);
            for (const TRef<Component>& component : entity->getAllComponents())
                mapNode(component->getGuid(), component.get());
            unmarkNodeAdded(entity);
        }

        for (auto& [component, templateOwnerEntity] : individualComponents)
        {
            Component* templateComponent = static_cast<Component*>(context.findCopy(component));
            templateOwnerEntity->attachComponent(templateComponent);
            mapNode(component->getGuid(), component);
            unmarkNodeAdded(component);
        }
    }

    template <> Type* Reflection::createType<PrefabInstance::Override>()
    {
        auto s = new TStructure<PrefabInstance::Override>("PrefabInstance.Override");
        s->addProperty("Template Node Id", &PrefabInstance::Override::templateNodeId);
        s->addProperty("Property Name", &PrefabInstance::Override::propertyName);
        return s;
    }

    template <> Type* Reflection::createType<PrefabInstance>()
    {
        auto c = new TClass<PrefabInstance, Component>("PrefabInstance");
        c->setDisplayName("Prefab Instance");
        c->addProperty("Source Prefab", &PrefabInstance::mSourcePrefab);
        c->addProperty("Node Map", &PrefabInstance::mNodeMap);
        c->addProperty("Overrides", &PrefabInstance::mOverrides);
        c->addProperty("Removed Nodes", &PrefabInstance::mRemovedNodes);
        c->addProperty("Added Nodes", &PrefabInstance::mAddedNodes);
        c->addProperty("Synced Version", &PrefabInstance::mSyncedVersion);
        return c;
    }
}
