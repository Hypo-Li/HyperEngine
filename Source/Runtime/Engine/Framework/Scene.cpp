#include "Scene.h"
#include "../Context.h"

#include <algorithm>

#include <RenderBase/RenderFrontend.h>
#include <Renderer/SceneProxy.h>
#include <Renderer/CameraProxy.h>
#include <Renderer/SceneRenderer.h>

namespace hyp
{
    REGISTER_CLASS(Scene)

    Scene::Scene()
    {
    }

    Scene::~Scene()
    {
        for (Entity* entity : mEntities)
            mPendingDestroyEntities.push_back(entity);
        processPending();

        assert(mPendingUnregisterComponents.empty());
        assert(mPendingRegisterComponents.empty());

        if (mProxyHandle.isValid())
        {
            destroySceneProxy(mProxyHandle);
            Context::get().getRenderFrontend().freeProxyHandle(mProxyHandle);
            mProxyHandle.reset();
        }
    }

    Entity* Scene::createEntity(HString name)
    {
        TRef<Entity> newEntity = createObject<Entity>(this);
        setEntityName(newEntity, name);
        mEntities.emplace_back(newEntity);
        return newEntity;
    }

    void Scene::setEntityName(Entity* entity, HString name)
    {
        if (entity->mName == name)
            return;

        entity->mName = makeUniqueName(name);
        mExistingNames.emplace(entity->mName);
    }

    bool Scene::destroyEntity(Entity* entity, bool destroyChildren)
    {
        if (!entity || entity->getScene() != this)
            return false;

        // 防止重复入队
        if (std::find(
            mPendingDestroyEntities.begin(),
            mPendingDestroyEntities.end(),
            entity) != mPendingDestroyEntities.end())
            return true;

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

        mPendingDestroyEntities.push_back(entity);
        return true;
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

    void Scene::tick(float deltaTime)
    {
        processPending();

        if (isPlaying())
        {
            for (auto& tickGroup : mComponentTickGroups)
            {
                for (auto component : tickGroup)
                {
                    component->onTick(deltaTime);
                }
            }
        }

        for (auto& dependency : mTransformDependencies)
        {
            dependency.notifyIfUpdated();
        }

        SceneProxyHandle proxyHandle = getOrCreateSceneProxy();
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle](RenderFrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SceneProxy* sceneProxy = proxyManager.getProxy(proxyHandle);
                auto& sceneRendererContext = Context::get().getSceneRendererContext();
                SceneRenderer sceneRenderer(sceneRendererContext);

                for (CameraProxy* cameraProxy : sceneProxy->getCameraProxies())
                {
                    TextureProxy* rt = cameraProxy->getRenderTarget();
                    if (rt)
                    {
                        // import render target textures
                        auto& rg = context.getRenderGraph();
                        rg::TextureHandle rtTextureHandle = rg.importTexture(cameraProxy->getRenderTargetName(), &rt->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                        rg.getBlackboard().put(cameraProxy->getRenderTargetName(), rtTextureHandle);
                    }
                    else
                    {
                        if (!Context::get().isEditor())
                        {
                            // 为无 RenderTarget 的 Camera 设置默认 render target extent
                            cameraProxy->setRenderTargetExtent(context.getSwapchainExtent());
                        }
                    }
                }

                auto sceneViewGroups = sceneProxy->gatherSceneViewGroups(sceneRendererContext.getViewDataManager());
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
        mPlayState = state;
    }

    HString Scene::makeUniqueName(HString name)
    {
        // 如果名字不冲突，直接返回并插入
        if (!mExistingNames.contains(name))
        {
            mExistingNames.emplace(name);
            return name;
        }

        // 发生冲突, 从1开始递增 Number 寻找可用后缀
        HString uniqueName = name;
        uint16_t counter = 1;

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
        if (!mPendingDestroyEntities.empty())
        {
            for (Entity* entity : mPendingDestroyEntities)
            {
                entity->removeAllComponents();
            }
        }

        if (!mPendingUnregisterComponents.empty())
        {
            auto pendingUnregisterComponents = std::move(mPendingUnregisterComponents);
            for (auto component : pendingUnregisterComponents)
            {
                component->onUnregister();
                component->setState(Component::State::Unregistered);

                size_t tickGroupIndex = static_cast<size_t>(component->getTickGroup());
                auto it = std::find(mComponentTickGroups[tickGroupIndex].begin(), mComponentTickGroups[tickGroupIndex].end(), component);
                assert(it != mComponentTickGroups[tickGroupIndex].end());
                mComponentTickGroups[tickGroupIndex].erase(it);
            }
        }

        if (!mPendingDestroyEntities.empty())
        {
            for (Entity* entity : mPendingDestroyEntities)
            {
                auto it = std::find(mEntities.begin(), mEntities.end(), entity);
                assert(it != mEntities.end());
                mEntities.erase(it);
            }
            mPendingDestroyEntities.clear();
        }

        if (!mPendingRegisterComponents.empty())
        {
            bool needSorts[kTickGroupCount] = { false };
            auto pendingRegisterComponents = std::move(mPendingRegisterComponents);
            for (auto component : pendingRegisterComponents)
            {
                component->onRegister();
                component->setState(Component::State::Registered);

                size_t tickGroupIndex = static_cast<size_t>(component->getTickGroup());
                mComponentTickGroups[tickGroupIndex].emplace_back(component);
                needSorts[tickGroupIndex] = true;
            }

            for (size_t i = 0; i < kTickGroupCount; i++)
            {
                if (!needSorts[i])
                    continue;
                std::sort(
                    mComponentTickGroups[i].begin(),
                    mComponentTickGroups[i].end(),
                    [](Component* a, Component* b)
                    {
                        size_t aHash = a->getClass()->getName().getTypeHash();
                        size_t bHash = b->getClass()->getName().getTypeHash();
                        return aHash < bHash;
                    }
                );
            }
        }
    }

    void Scene::createSceneProxy(SceneProxyHandle handle)
    {
        Context::get().getRenderFrontend().enqueueLambda(
            [handle](RenderFrameContext& context)
            {
                context.getProxyManager().createProxy(
                    handle,
                    Context::get().getSceneRendererContext().getViewDataManager(),
                    0
                );
            }
        );
    }

    void Scene::destroySceneProxy(SceneProxyHandle handle)
    {
        assert(handle.isValid());

        Context::get().getRenderFrontend().enqueueLambda(
            [handle](RenderFrameContext& context)
            {
                context.getProxyManager().destroyProxy(handle);
            }
        );
    }

    template <> Type* Reflection::createType<Scene>()
    {
        auto c = new TClass<Scene, Object, AllocateFrequency::Low>("Scene");
        c->addProperty("Entities", &Scene::mEntities);
        return c;
    }
}
