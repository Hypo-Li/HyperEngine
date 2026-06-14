#pragma once
#include <unordered_map>

#include "Entity.h"

#include <RenderBase/ProxyHandle.h>

namespace hyp
{
    enum class PlayState
    {
        Stoped,
        Playing,
        Paused,
    };

    class Scene : public Object
    {
        REFLECT_CLASS(Scene)
    public:
        Scene();

        virtual ~Scene() noexcept;

        Entity* createEntity(HString name);

        void setEntityName(Entity* entity, HString name);

        bool destroyEntity(Entity* entity, bool destroyChildren = false);

        void registerComponent(Component* component);

        void unregisterComponent(Component* component);

        void tick(float deltaTime);

        void registerTransformListener(Transform* transform, ITransformListener* listener);

        void unregisterTransformListener(Transform* transform, ITransformListener* listener);

        SceneProxyHandle getOrCreateSceneProxy();

        void setPlayState(PlayState state);

        PlayState getPlayState() const noexcept
        {
            return mPlayState;
        }

        bool isPlaying() const noexcept
        {
            return mPlayState == PlayState::Playing;
        }

    private:
        HString makeUniqueName(HString name);

        void processPending();

        void createSceneProxy(SceneProxyHandle handle);

        void destroySceneProxy(SceneProxyHandle handle);

    private:
        std::vector<TRef<Entity>> mEntities;
        std::unordered_set<HString> mExistingNames;

        using Components = std::vector<TRef<Component>>;
        static constexpr size_t kTickGroupCount = static_cast<size_t>(TickGroup::Count);
        Components mComponentTickGroups[kTickGroupCount];

        std::vector<Component*> mPendingRegisterComponents;
        std::vector<Component*> mPendingUnregisterComponents;
        std::vector<Entity*> mPendingDestroyEntities;

        struct TransformDependency
        {
            Transform* transform;
            uint32_t cachedVersion;
            ITransformListener* listener;

            inline void notifyIfUpdated()
            {
                if (transform->getVersion() != cachedVersion)
                {
                    listener->onTransformUpdate(*transform);
                    cachedVersion = transform->getVersion();
                }
            }
        };
        std::vector<TransformDependency> mTransformDependencies;

        SceneProxyHandle mProxyHandle;

        PlayState mPlayState = PlayState::Stoped;
    };

    template <> Type* Reflection::createType<Scene>();
}
