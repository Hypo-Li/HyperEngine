#include "Scene.h"
#include "../Context.h"
#include "../Component/Collider/Collider.h"
#include "../Physics/PhysicsSystem.h"

#include <RenderBase/RenderFrontend.h>
#include <Renderer/SceneProxy.h>
#include <Renderer/CameraProxy.h>
#include <Renderer/SceneRenderer.h>

#include <algorithm>
#include <cstdarg>
#include <functional>
#include <unordered_set>

namespace hyp
{
    namespace
    {
        // 按 Class* 地址排序——只是为了让同一批组件每次 tick 的相对顺序稳定, 不代表类型
        // 之间有什么优先级含义。
        bool compareComponentByClass(Component* a, Component* b)
        {
            auto aPtr = reinterpret_cast<std::uintptr_t>(a->getClass());
            auto bPtr = reinterpret_cast<std::uintptr_t>(b->getClass());
            return aPtr < bPtr;
        }
    }

    REGISTER_CLASS(Scene)

    Scene::Scene()
    {
    }

    Scene::~Scene()
    {
        // onEndPlay()/onUnregister() 里新入队的实体和组件, 单次 processPending() 处理不完
        do
        {
            for (TRef<Entity>& entity : mEntities)
                mPendingDestroyEntities.push_back(std::move(entity));
            mEntities.clear();
            mEntityByGuid.clear();
            mExistingNames.clear();
            processPending();
        }
        while (!mEntities.empty() || !mPendingDestroyEntities.empty()
            || !mPendingUnregisterComponents.empty() || !mPendingRegisterComponents.empty());

        assert(mPendingUnregisterComponents.empty());
        assert(mPendingRegisterComponents.empty());

        if (mPhysicsScene)
            Context::get().getPhysicsSystem().destroyScene(mPhysicsScene);

        if (mProxyHandle.isValid())
        {
            destroySceneProxy(mProxyHandle);
            Context::get().getRenderFrontend().freeProxyHandle(mProxyHandle);
            mProxyHandle.reset();
        }
    }

    void Scene::finalizeLoad()
    {
        // mEntities 本身是反射属性, 反序列化 Scene 这个对象的时候已经被直接填好了(跟
        // Entity::mComponents 被自己的反序列化直接填好是同一个道理), 这里不需要、也不能再
        // emplace_back 一遍——只需要补上反射没有覆盖到的部分: mExistingNames/mEntityByGuid
        // 都不是反射属性, 要重新建一遍(顺手用 makeUniqueName() 兜底一下文件本身如果真的存了
        // 重复名字的情况, 不假设存盘时一定是干净的)。每个 Entity 自己的 Component 注册留给
        // Entity::finalizeLoad() 处理, 不在这里重复做。
        for (const TRef<Entity>& entity : mEntities)
        {
            // entity 是 const TRef<Entity>&, operator->() 会走 const 重载、拿到 const
            // Entity*, 改不了 mName——用 get() 拿裸指针绕开这层(TRef::get() 始终返回非 const
            // 的 T*, 跟 TRef 自身的 const 性无关)
            Entity* rawEntity = entity.get();
            rawEntity->mName = makeUniqueName(rawEntity->mName);
            mEntityByGuid.emplace(rawEntity->getGuid(), rawEntity);
        }

        sanitizeFolders();
    }

    Entity* Scene::createEntity(HString name)
    {
        TRef<Entity> newEntity = createObject<Entity>(this);
        setEntityName(newEntity, name);
        mEntities.emplace_back(newEntity);
        mEntityByGuid.emplace(newEntity->getGuid(), newEntity.get());
        return newEntity;
    }

    void Scene::registerEntity(Entity* entity)
    {
        // 不走 setEntityName(): 那个接口是"改名, 新名字跟当前相同就什么都不做"的语义, 对
        // "把复制出来的、名字字符串跟原对象一字不差的 Entity 接进来"这种场景不适用(原对象自己
        // 也在这个 Scene 里的话, 会误判"没有变化"直接跳过, 撞名冲突永远不会被消解)。
        // makeUniqueName() 本身就会把最终选定的名字插进 mExistingNames, 直接用它的结果赋值
        // 就够了。
        entity->mName = makeUniqueName(entity->mName);
        mEntities.emplace_back(entity);
        mEntityByGuid.emplace(entity->getGuid(), entity);

        for (const TRef<Component>& component : entity->getAllComponents())
            registerComponent(component.get());
    }

    Entity* Scene::findEntity(Guid guid) const noexcept
    {
        auto it = mEntityByGuid.find(guid);
        return it != mEntityByGuid.end() ? it->second : nullptr;
    }

    size_t Scene::getRootEntityIndex(const Entity* entity) const noexcept
    {
        if (!entity || entity->getScene() != this || entity->getParent())
            return kInvalidIndex;

        if (auto filed = mFolderOfEntity.find(entity); filed != mFolderOfEntity.end())
        {
            const Folder* folder = findFolder(filed->second);
            assert(folder);
            auto position = std::find(folder->entities.begin(), folder->entities.end(), entity);
            return static_cast<size_t>(position - folder->entities.begin());
        }

        size_t rootIndex = 0;
        for (const TRef<Entity>& candidate : mEntities)
        {
            if (candidate.get() == entity)
                return rootIndex;
            if (!candidate->getParent() && !mFolderOfEntity.contains(candidate.get()))
                ++rootIndex;
        }
        return kInvalidIndex;
    }

    bool Scene::moveRootEntity(Entity* entity, size_t index)
    {
        if (!entity || entity->getScene() != this || entity->getParent())
            return false;

        if (auto filed = mFolderOfEntity.find(entity); filed != mFolderOfEntity.end())
        {
            Folder* folder = findFolderMutable(filed->second);
            assert(folder);
            folder->entities.erase(std::find(folder->entities.begin(), folder->entities.end(), entity));
            folder->entities.insert(folder->entities.begin() + std::min(index, folder->entities.size()), entity);
            return true;
        }

        auto from = std::find(mEntities.begin(), mEntities.end(), entity);
        if (from == mEntities.end())
            return false;

        TRef<Entity> keepAlive = *from;
        mEntities.erase(from);

        auto to = mEntities.end();
        size_t seenRoots = 0;
        for (auto it = mEntities.begin(); it != mEntities.end(); ++it)
        {
            if ((*it)->getParent() || mFolderOfEntity.contains(it->get()))
                continue;
            if (seenRoots == index)
            {
                to = it;
                break;
            }
            ++seenRoots;
        }
        mEntities.insert(to, keepAlive);
        return true;
    }

    const Scene::Folder* Scene::findFolder(HString name) const noexcept
    {
        for (const Folder& folder : mFolders)
        {
            if (folder.name == name)
                return &folder;
        }
        return nullptr;
    }

    Scene::Folder* Scene::findFolderMutable(HString name) noexcept
    {
        for (Folder& folder : mFolders)
        {
            if (folder.name == name)
                return &folder;
        }
        return nullptr;
    }

    HString Scene::getFolderOfEntity(const Entity* entity) const noexcept
    {
        auto it = mFolderOfEntity.find(entity);
        return it != mFolderOfEntity.end() ? it->second : HString();
    }

    HString Scene::getParentFolder(HString folderName) const noexcept
    {
        for (const Folder& folder : mFolders)
        {
            if (std::find(folder.childFolders.begin(), folder.childFolders.end(), folderName) != folder.childFolders.end())
                return folder.name;
        }
        return HString();
    }

    size_t Scene::getFolderIndex(HString name) const noexcept
    {
        auto rootEntry = std::find(mRootFolders.begin(), mRootFolders.end(), name);
        if (rootEntry != mRootFolders.end())
            return static_cast<size_t>(rootEntry - mRootFolders.begin());

        for (const Folder& folder : mFolders)
        {
            auto entry = std::find(folder.childFolders.begin(), folder.childFolders.end(), name);
            if (entry != folder.childFolders.end())
                return static_cast<size_t>(entry - folder.childFolders.begin());
        }
        return kInvalidIndex;
    }

    HString Scene::makeUniqueFolderName(HString name) const
    {
        if (findFolder(name) == nullptr)
            return name;

        HString uniqueName = name;
        for (uint16_t counter = 2; ; ++counter)
        {
            uniqueName.setNumber(counter);
            if (findFolder(uniqueName) == nullptr)
                return uniqueName;
        }
    }

    std::vector<HString>& Scene::folderListOf(HString parentName)
    {
        if (parentName.empty())
            return mRootFolders;

        Folder* parent = findFolderMutable(parentName);
        assert(parent);
        return parent->childFolders;
    }

    bool Scene::isFolderInSubtree(HString ancestor, HString folder) const noexcept
    {
        if (ancestor == folder)
            return true;

        const Folder* root = findFolder(ancestor);
        if (!root)
            return false;

        for (const HString& child : root->childFolders)
        {
            if (isFolderInSubtree(child, folder))
                return true;
        }
        return false;
    }

    HString Scene::createFolder(HString name, HString parentName)
    {
        if (name.empty() || (!parentName.empty() && !findFolder(parentName)))
            return HString();

        HString finalName = makeUniqueFolderName(name);
        mFolders.push_back(Folder{ finalName, {}, {} });
        folderListOf(parentName).push_back(finalName);
        return finalName;
    }

    HString Scene::renameFolder(HString oldName, HString newName)
    {
        Folder* folder = findFolderMutable(oldName);
        if (!folder || newName.empty())
            return HString();
        if (newName == oldName)
            return oldName;

        HString finalName = makeUniqueFolderName(newName);

        std::vector<HString>& siblings = folderListOf(getParentFolder(oldName));
        auto entry = std::find(siblings.begin(), siblings.end(), oldName);
        assert(entry != siblings.end());
        *entry = finalName;

        folder->name = finalName;
        for (Entity* member : folder->entities)
            mFolderOfEntity[member] = finalName;
        return finalName;
    }

    bool Scene::moveFolderToFolder(HString name, HString parentName)
    {
        if (!findFolder(name))
            return false;
        if (!parentName.empty() && (!findFolder(parentName) || isFolderInSubtree(name, parentName)))
            return false;

        HString currentParent = getParentFolder(name);
        if (currentParent == parentName)
            return true;

        std::vector<HString>& source = folderListOf(currentParent);
        source.erase(std::find(source.begin(), source.end(), name));
        folderListOf(parentName).push_back(name);
        return true;
    }

    bool Scene::moveFolder(HString name, size_t index)
    {
        if (!findFolder(name))
            return false;

        std::vector<HString>& siblings = folderListOf(getParentFolder(name));
        siblings.erase(std::find(siblings.begin(), siblings.end(), name));
        siblings.insert(siblings.begin() + std::min(index, siblings.size()), name);
        return true;
    }

    bool Scene::removeFolder(HString name)
    {
        Folder* folder = findFolderMutable(name);
        if (!folder)
            return false;

        const HString parentName = getParentFolder(name);
        std::vector<HString> childFolders = std::move(folder->childFolders);
        std::vector<Entity*> members = std::move(folder->entities);

        std::vector<HString>& siblings = folderListOf(parentName);
        auto entry = std::find(siblings.begin(), siblings.end(), name);
        const size_t position = static_cast<size_t>(entry - siblings.begin());
        siblings.erase(entry);
        siblings.insert(siblings.begin() + position, childFolders.begin(), childFolders.end());

        Folder* parent = parentName.empty() ? nullptr : findFolderMutable(parentName);
        for (Entity* member : members)
        {
            if (parent)
            {
                parent->entities.push_back(member);
                mFolderOfEntity[member] = parentName;
            }
            else
            {
                mFolderOfEntity.erase(member);
            }
        }

        mFolders.erase(mFolders.begin() + (folder - mFolders.data()));
        return true;
    }

    bool Scene::moveEntityToFolder(Entity* entity, HString folderName)
    {
        if (folderName.empty())
            return removeEntityFromFolder(entity);

        if (!entity || findEntity(entity->getGuid()) != entity || entity->getParent())
            return false;

        Folder* folder = findFolderMutable(folderName);
        if (!folder)
            return false;

        if (auto filed = mFolderOfEntity.find(entity); filed != mFolderOfEntity.end())
        {
            if (filed->second == folderName)
                return true;

            Folder* previous = findFolderMutable(filed->second);
            assert(previous);
            previous->entities.erase(std::find(previous->entities.begin(), previous->entities.end(), entity));
        }

        folder->entities.push_back(entity);
        mFolderOfEntity[entity] = folderName;
        return true;
    }

    void Scene::unfileEntity(const Entity* entity)
    {
        auto filed = mFolderOfEntity.find(entity);
        if (filed == mFolderOfEntity.end())
            return;

        Folder* folder = findFolderMutable(filed->second);
        assert(folder);
        folder->entities.erase(std::find(folder->entities.begin(), folder->entities.end(), entity));
        mFolderOfEntity.erase(filed);
    }

    bool Scene::removeEntityFromFolder(Entity* entity)
    {
        if (!entity || !mFolderOfEntity.contains(entity))
            return false;

        unfileEntity(entity);
        if (!entity->getParent())
            moveRootEntity(entity, kInvalidIndex);
        return true;
    }

    void Scene::onEntityReparented(Entity* entity)
    {
        if (entity->getParent())
            unfileEntity(entity);
    }

    void Scene::sanitizeFolders()
    {
        // 丢弃空名字; 重名的后续项改成唯一名字(它们变成没人引用的孤儿, 下面会挂回根)
        std::vector<Folder> loaded = std::move(mFolders);
        mFolders.clear();
        for (Folder& folder : loaded)
        {
            if (folder.name.empty())
                continue;
            folder.name = makeUniqueFolderName(folder.name);
            mFolders.push_back(std::move(folder));
        }

        // 从根开始遍历: 引用了不存在的 Folder、或者被重复引用(包括环)的条目一律摘掉
        std::unordered_set<HString> visited;
        std::function<void(std::vector<HString>&)> visitList = [&](std::vector<HString>& list)
        {
            for (size_t i = 0; i < list.size();)
            {
                Folder* child = findFolderMutable(list[i]);
                if (!child || !visited.insert(list[i]).second)
                {
                    list.erase(list.begin() + i);
                    continue;
                }
                visitList(child->childFolders);
                ++i;
            }
        };
        visitList(mRootFolders);

        // 没有被任何地方引用的 Folder 挂回根
        for (const Folder& folder : mFolders)
        {
            if (!visited.insert(folder.name).second)
                continue;
            mRootFolders.push_back(folder.name);
            visitList(findFolderMutable(folder.name)->childFolders);
        }

        mFolderOfEntity.clear();
        for (Folder& folder : mFolders)
        {
            for (size_t i = 0; i < folder.entities.size();)
            {
                Entity* member = folder.entities[i];
                if (!member || findEntity(member->getGuid()) != member || member->getParent() ||
                    !mFolderOfEntity.emplace(member, folder.name).second)
                {
                    folder.entities.erase(folder.entities.begin() + i);
                    continue;
                }
                ++i;
            }
        }
    }

    Component* Scene::findComponent(Guid guid) const noexcept
    {
        auto it = mComponentByGuid.find(guid);
        return it != mComponentByGuid.end() ? it->second : nullptr;
    }

    void Scene::setEntityName(Entity* entity, HString name)
    {
        if (entity->mName == name)
            return;

        // 已经销毁(只剩待销毁列表在持有)的 Entity 不在名字集合里, 不能去动别人的名字
        if (std::find(mPendingDestroyEntities.begin(), mPendingDestroyEntities.end(), entity) != mPendingDestroyEntities.end())
        {
            entity->mName = name;
            return;
        }

        mExistingNames.erase(entity->mName);
        entity->mName = makeUniqueName(name);
        mExistingNames.emplace(entity->mName);
    }

    bool Scene::destroyEntity(Entity* entity, bool destroyChildren)
    {
        if (!entity || entity->getScene() != this)
            return false;

        // 防止重复入队: 已经在待销毁列表里的不在 mEntities 里
        if (std::find(mEntities.begin(), mEntities.end(), entity) == mEntities.end())
        {
            return std::find(mPendingDestroyEntities.begin(), mPendingDestroyEntities.end(), entity)
                != mPendingDestroyEntities.end();
        }

        size_t childCount = entity->getChildCount();
        if (childCount > 0)
        {
            if (destroyChildren)
            {
                while (childCount--)
                    destroyEntity(entity->getChild(0), true);
            }
            else
            {
                while (childCount--)
                    entity->getChild(0)->setParent(entity->getParent(), ReparentMode::Keep_World);
            }
        }

        entity->setParent(nullptr, ReparentMode::Keep_Local);

        // 上面递归销毁子节点会改动 mEntities, 迭代器必须在这里重新找
        auto it = std::find(mEntities.begin(), mEntities.end(), entity);
        assert(it != mEntities.end());
        mPendingDestroyEntities.push_back(std::move(*it));
        mEntities.erase(it);
        mEntityByGuid.erase(entity->getGuid());
        mExistingNames.erase(entity->getName());
        unfileEntity(entity);
        return true;
    }

    void Scene::tickComponentGroup(TickGroup group, float deltaTime)
    {
        for (auto component : mComponentTickGroups[static_cast<size_t>(group)])
        {
            component->onTick(deltaTime);
        }
    }

    void Scene::registerComponent(Component* component)
    {
        if (component->getState() == Component::State::Initial)
        {
            mPendingRegisterComponents.emplace_back(component);
            component->setState(Component::State::Pending_Register);
        }
    }

    void Scene::unregisterComponent(Component* component)
    {
        if (component->getState() == Component::State::Registered)
        {
            mPendingUnregisterComponents.emplace_back(component);
            component->setState(Component::State::Pending_Unregister);
        }
        else if (component->getState() == Component::State::Pending_Register)
        {
            auto it = std::find(mPendingRegisterComponents.begin(), mPendingRegisterComponents.end(), component);
            assert(it != mPendingRegisterComponents.end());
            mPendingRegisterComponents.erase(it);
        }
    }

    void Scene::moveComponentTickGroup(Component* component, TickGroup oldGroup, TickGroup newGroup)
    {
        auto& oldGroupComponents = mComponentTickGroups[static_cast<size_t>(oldGroup)];
        auto it = std::find(oldGroupComponents.begin(), oldGroupComponents.end(), component);
        assert(it != oldGroupComponents.end());
        oldGroupComponents.erase(it);

        auto& newGroupComponents = mComponentTickGroups[static_cast<size_t>(newGroup)];
        newGroupComponents.emplace_back(component);
        std::sort(newGroupComponents.begin(), newGroupComponents.end(), compareComponentByClass);
    }

    void Scene::tick(float deltaTime)
    {
        processPending();

        if (isPlaying() || mStepRequested)
        {
            tickComponentGroup(TickGroup::Pre_Physics, deltaTime);
            tickComponentGroup(TickGroup::Physics, deltaTime);

            // 固定步长的推进逻辑(accumulator)在 PhysicsScene::update() 内部, 这里只管喂真实
            // deltaTime——这一步不是 Component::onTick(), 是插在 Physics/Post_Physics 两个
            // tick group 之间的引擎内部步骤, 让 Post_Physics 组的组件(比如 Dynamic Collider
            // 读回位姿)能看到这一帧的最新模拟结果。
            if (mPhysicsScene)
                mPhysicsScene->update(deltaTime);

            tickComponentGroup(TickGroup::Post_Physics, deltaTime);

            if (mPhysicsScene)
                dispatchOverlapEvents(mPhysicsScene->takeOverlapEvents());

            mStepRequested = false;
        }

        for (auto& dependency : mTransformDependencies)
        {
            dependency.notifyIfUpdated();
        }

        SceneProxyHandle proxyHandle = getOrCreateSceneProxy();
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SceneProxy* sceneProxy = proxyManager.getProxy(proxyHandle);
                auto& rendererContext = Context::get().getRendererContext();
                SceneRenderer sceneRenderer(rendererContext);

                for (CameraProxy* cameraProxy : sceneProxy->getCameraProxies())
                {
                    TextureProxy* rt = cameraProxy->getRenderTarget();
                    if (rt)
                    {
                        // import render target textures
                        auto& rg = context.getRenderGraph();
                        rg::TextureHandle rtTextureHandle = rg.importTexture(cameraProxy->getRenderTargetName(), &rt->getTextureView(), rhi::ResourceState::Shader_Sampled);
                        rg.getBlackboard().put(cameraProxy->getRenderTargetName(), rtTextureHandle);
                    }
                    else
                    {
                        if (!Context::get().isEditor())
                        {
                            // 非编辑器模式下, 每帧为无 RenderTarget 的 Camera 设置 swapchain extent
                            cameraProxy->setRenderTargetExtent(context.getSwapchainExtent());
                        }
                    }
                }

                auto sceneViewGroups = sceneProxy->gatherSceneViewGroups(rendererContext.getViewDataManager(), context.getFrameIndex());
                sceneRenderer.render(*sceneProxy, sceneViewGroups, context.getRenderGraph());
            }
        );
    }

    void Scene::registerTransformListener(Transform* transform, ITransformListener* listener)
    {
        mTransformDependencies.emplace_back(transform, transform->getVersion(), listener);
    }

    void Scene::unregisterTransformListener(Transform* transform, ITransformListener* listener)
    {
        auto it = std::find_if(
            mTransformDependencies.begin(),
            mTransformDependencies.end(),
            [transform, listener](const TransformDependency& dependency)
            {
                return dependency.transform == transform && dependency.listener == listener;
            }
        );
        assert(it != mTransformDependencies.end());
        mTransformDependencies.erase(it);
    }

    SceneProxyHandle Scene::getOrCreateSceneProxy()
    {
        if (!mProxyHandle.isValid())
        {
            mProxyHandle = Context::get().getRenderFrontend().allocateProxyHandle<ProxyType::Scene>();
            createSceneProxy(mProxyHandle);
        }
        return mProxyHandle;
    }

    void Scene::setPlayState(PlayState state)
    {
        bool wasActive = mPlayState != PlayState::Stoped;
        bool willBeActive = state != PlayState::Stoped;

        // PhysicsScene 只在会话期间存在, Stop 时整份丢弃, 场上所有 body 一起清掉, 不需要逐个 destroyBody()。
        if (!wasActive && willBeActive)
        {
            mPlayState = state;
            mPhysicsScene = Context::get().getPhysicsSystem().createScene();

            for (size_t i = 0; i < kTickGroupCount; i++)
                for (TRef<Component>& component : mComponentTickGroups[i])
                    component->onBeginPlay();
        }
        else
        {
            if (wasActive && !willBeActive)
            {
                for (size_t i = 0; i < kTickGroupCount; i++)
                    for (TRef<Component>& component : mComponentTickGroups[i])
                        component->onEndPlay();

                Context::get().getPhysicsSystem().destroyScene(mPhysicsScene);
                mPhysicsScene = nullptr;
            }

            mPlayState = state;
        }
    }

    void Scene::requestStep()
    {
        assert(mPlayState == PlayState::Paused);
        mStepRequested = true;
    }

    void Scene::endColliderOverlaps(Collider* collider)
    {
        if (!mPhysicsScene)
            return;

        dispatchOverlapEvents(mPhysicsScene->releaseOverlaps(collider->getBodyHandle()));
    }

    namespace
    {
        PhysicsQueryParams makePhysicsQueryParams(const SceneQueryParams& params)
        {
            PhysicsQueryParams result;
            result.layerMask = params.layerMask;
            result.includeTriggers = params.includeTriggers;

            for (Collider* collider : params.ignoredColliders)
            {
                if (collider && collider->getBodyHandle().isValid())
                    result.ignoredBodies.push_back(collider->getBodyHandle());
            }

            for (Entity* entity : params.ignoredEntities)
            {
                if (!entity)
                    continue;

                for (const TRef<Component>& component : entity->getAllComponents())
                {
                    if (!component->getClass()->isDerivedFrom(Collider::staticClass()))
                        continue;

                    PhysicsBodyHandle bodyHandle = static_cast<Collider*>(component.get())->getBodyHandle();
                    if (bodyHandle.isValid())
                        result.ignoredBodies.push_back(bodyHandle);
                }
            }
            return result;
        }
    }

    bool Scene::raycast(double3 start, double3 end, SceneRaycastHit& outHit, const SceneQueryParams& params) const
    {
        if (!mPhysicsScene)
            return false;

        PhysicsRaycastHit hit;
        if (!mPhysicsScene->raycast(start, end, makePhysicsQueryParams(params), hit))
            return false;

        Collider* collider = static_cast<Collider*>(findComponent(hit.owner));
        if (!collider)
            return false;

        outHit.collider = collider;
        outHit.position = hit.position;
        outHit.normal = hit.normal;
        outHit.distance = hit.distance;
        return true;
    }

    std::vector<Collider*> Scene::overlapSphere(double3 center, float radius, const SceneQueryParams& params) const
    {
        std::vector<Collider*> colliders;
        if (!mPhysicsScene)
            return colliders;

        for (Guid owner : mPhysicsScene->overlapSphere(center, radius, makePhysicsQueryParams(params)))
        {
            if (Collider* collider = static_cast<Collider*>(findComponent(owner)))
                colliders.push_back(collider);
        }
        return colliders;
    }

    void Scene::dispatchOverlapEvents(std::span<const PhysicsOverlapEvent> events)
    {
        for (const PhysicsOverlapEvent& event : events)
        {
            Collider* trigger = static_cast<Collider*>(findComponent(event.trigger));
            Collider* other = static_cast<Collider*>(findComponent(event.other));
            if (!trigger || !other)
                continue;

            trigger->notifyOverlap(*other, event.begin);
            other->notifyOverlap(*trigger, event.begin);
        }
    }

    void Scene::setLumenEnabled(bool enabled)
    {
        if (mLumenEnabled == enabled)
            return;

        mLumenEnabled = enabled;

        SceneProxyHandle proxyHandle = getOrCreateSceneProxy();
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle, enabled](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SceneProxy* sceneProxy = proxyManager.getProxy(proxyHandle);
                sceneProxy->setLumenEnabled(enabled);
            }
        );

        // 通知所有已注册的监听方(比如 StaticMeshRenderer)重新检查/建立各自的 Lumen 相关
        // 状态——在 Lumen 关闭期间 onRegister() 过的实例, 当时懒加载判据没满足会直接跳过,
        // 没有别的时机能让它们补上这一步
        for (ILumenStateListener* listener : mLumenStateListeners)
            listener->onLumenEnabledChanged(enabled);
    }

    void Scene::registerLumenStateListener(ILumenStateListener* listener)
    {
        mLumenStateListeners.push_back(listener);
    }

    void Scene::unregisterLumenStateListener(ILumenStateListener* listener)
    {
        auto it = std::find(mLumenStateListeners.begin(), mLumenStateListeners.end(), listener);
        assert(it != mLumenStateListeners.end());
        mLumenStateListeners.erase(it);
    }

    void Scene::setSceneVisualizeMode(SceneVisualizeMode mode)
    {
        mSceneVisualizeMode = mode;

        SceneProxyHandle proxyHandle = getOrCreateSceneProxy();
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle, mode](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SceneProxy* sceneProxy = proxyManager.getProxy(proxyHandle);
                sceneProxy->setSceneVisualizeMode(mode);
            }
        );
    }

    void Scene::setUseMeshDistanceField(bool useMeshDistanceField)
    {
        mUseMeshDistanceField = useMeshDistanceField;

        SceneProxyHandle proxyHandle = getOrCreateSceneProxy();
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle, useMeshDistanceField](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SceneProxy* sceneProxy = proxyManager.getProxy(proxyHandle);
                sceneProxy->setUseMeshDistanceField(useMeshDistanceField);
            }
        );
    }

    void Scene::setDisableScreenTrace(bool disableScreenTrace)
    {
        mDisableScreenTrace = disableScreenTrace;

        SceneProxyHandle proxyHandle = getOrCreateSceneProxy();
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle, disableScreenTrace](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SceneProxy* sceneProxy = proxyManager.getProxy(proxyHandle);
                sceneProxy->setDisableScreenTrace(disableScreenTrace);
            }
        );
    }

    HString Scene::makeUniqueName(HString name)
    {
        // 如果名字不冲突，直接返回并插入
        if (!mExistingNames.contains(name))
        {
            mExistingNames.emplace(name);
            return name;
        }

        // 发生冲突, 从后缀 1 开始递增寻找可用名字
        // (Number 编码 = 后缀数字 + 1, 故后缀 1 对应 Number 2)
        HString uniqueName = name;
        uint16_t counter = 2;

        while (true)
        {
            uniqueName.setNumber(counter);
            if (!mExistingNames.contains(uniqueName))
            {
                mExistingNames.emplace(uniqueName);
                break;
            }

            counter++;
        }

        return uniqueName;
    }

    void Scene::processPending()
    {
        // onEndPlay() 里的 handler 可能再调 destroyEntity(), 新入队的留到下一次 processPending()。
        std::vector<TRef<Entity>> destroyingEntities = std::move(mPendingDestroyEntities);
        mPendingDestroyEntities.clear();

        for (TRef<Entity>& entity : destroyingEntities)
        {
            entity->removeAllComponents();
        }

        if (!mPendingUnregisterComponents.empty())
        {
            auto pendingUnregisterComponents = std::move(mPendingUnregisterComponents);

            // 先把这一批的 onEndPlay() 全部跑完, 再统一 onUnregister(), 避免 A 的 onEndPlay()
            // 读到已经被同一批次里 B 的 onUnregister() 拆掉的状态。
            if (mPlayState != PlayState::Stoped)
            {
                for (auto component : pendingUnregisterComponents)
                    component->onEndPlay();
            }

            for (auto component : pendingUnregisterComponents)
            {
                component->doUnregister();
                mComponentByGuid.erase(component->getGuid());

                size_t tickGroupIndex = static_cast<size_t>(component->getTickGroup());
                auto it = std::find(mComponentTickGroups[tickGroupIndex].begin(), mComponentTickGroups[tickGroupIndex].end(), component);
                assert(it != mComponentTickGroups[tickGroupIndex].end());
                mComponentTickGroups[tickGroupIndex].erase(it);
            }
        }

        destroyingEntities.clear();

        if (!mPendingRegisterComponents.empty())
        {
            bool needSorts[kTickGroupCount] = { false };
            auto pendingRegisterComponents = std::move(mPendingRegisterComponents);
            for (auto component : pendingRegisterComponents)
            {
                component->doRegister();
                mComponentByGuid.emplace(component->getGuid(), component);

                size_t tickGroupIndex = static_cast<size_t>(component->getTickGroup());
                mComponentTickGroups[tickGroupIndex].emplace_back(component);
                needSorts[tickGroupIndex] = true;
            }

            for (size_t i = 0; i < kTickGroupCount; i++)
            {
                if (!needSorts[i])
                    continue;
                std::sort(mComponentTickGroups[i].begin(), mComponentTickGroups[i].end(), compareComponentByClass);
            }

            // 这一批 onRegister() 全部跑完之后再统一 onBeginPlay(), 避免 A 的 onBeginPlay()
            // 读到同一批次里还没 onRegister() 的 B 的半成品状态(比如同一帧生成的两个 Entity
            // 互相引用)。
            if (mPlayState != PlayState::Stoped)
            {
                for (auto component : pendingRegisterComponents)
                    component->onBeginPlay();
            }
        }
    }

    void Scene::createSceneProxy(SceneProxyHandle handle)
    {
        Context::get().getRenderFrontend().enqueueLambda(
            [handle](FrameContext& context)
            {
                auto& rendererContext = Context::get().getRendererContext();
                context.getProxyManager().createProxy(handle, handle.getIndex(), rendererContext.getIndicesBuffer().getBuffer());
            }
        );
    }

    void Scene::destroySceneProxy(SceneProxyHandle handle)
    {
        assert(handle.isValid());

        Context::get().getRenderFrontend().enqueueLambda(
            [handle](FrameContext& context)
            {
                context.getProxyManager().destroyProxy(handle);
            }
        );
    }

    template <> Type* Reflection::createType<Scene::Folder>()
    {
        auto s = new TStructure<Scene::Folder>("Scene.Folder");
        s->addProperty("Name", &Scene::Folder::name);
        s->addProperty("Child Folders", &Scene::Folder::childFolders);
        s->addProperty("Entities", &Scene::Folder::entities);
        return s;
    }

    template <> Type* Reflection::createType<Scene>()
    {
        auto c = new TClass<Scene, Object, AllocateFrequency::Low>("Scene");
        c->addProperty("Entities", &Scene::mEntities);
        c->addProperty("Folders", &Scene::mFolders);
        c->addProperty("Root Folders", &Scene::mRootFolders);
        return c;
    }
}
