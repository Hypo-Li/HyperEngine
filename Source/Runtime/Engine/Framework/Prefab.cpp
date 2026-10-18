#include "Prefab.h"

#include "../Component/PrefabInstance.h"
#include "Scene.h"

#include <Core/Serialization/CopyContext.h>
#include <Core/Serialization/Copier.h>

#include <algorithm>
#include <cassert>

namespace hyp
{
    REGISTER_CLASS(Prefab)

    namespace
    {
        void collectSubtreeEntities(Entity* entity, std::vector<Entity*>& outEntities)
        {
            outEntities.emplace_back(entity);
            for (size_t i = 0; i < entity->getChildCount(); ++i)
                collectSubtreeEntities(entity->getChild(i), outEntities);
        }
    }

    TRef<Prefab> Prefab::createFromEntity(Entity* rootEntity)
    {
        TRef<Prefab> prefab = createObject<Prefab>(nullptr);

        std::vector<Entity*> entities;
        collectSubtreeEntities(rootEntity, entities);

        CopyContext context;
        Copier copier(context);
        for (Entity* entity : entities)
            copier.copy(entity, prefab.get());
        copier.finalizeAll();

        for (Entity* entity : entities)
            prefab->mEntities.emplace_back(static_cast<Entity*>(context.findCopy(entity)));
        prefab->mRootEntity = static_cast<Entity*>(context.findCopy(rootEntity));

        return prefab;
    }

    Entity* Prefab::instantiate(Scene* scene)
    {
        CopyContext context;
        Copier copier(context);

        for (const TRef<Entity>& templateEntity : mEntities)
            copier.copy(templateEntity.get(), scene);
        copier.finalizeAll();

        Entity* runtimeRoot = static_cast<Entity*>(context.findCopy(mRootEntity));

        PrefabInstance* instance = runtimeRoot->addComponent<PrefabInstance>();
        instance->setSourcePrefab(this);

        for (const TRef<Entity>& templateEntity : mEntities)
        {
            instance->mapNode(templateEntity->getGuid(), context.findCopy(templateEntity.get()));
            for (const TRef<Component>& templateComponent : templateEntity->getAllComponents())
                instance->mapNode(templateComponent->getGuid(), context.findCopy(templateComponent.get()));
        }

        return runtimeRoot;
    }

    void Prefab::apply()
    {
        ++mVersion;

        for (PrefabInstance* instance : mLiveInstances)
        {
            Scene* scene = instance->getScene();
            if (scene && scene->getPlayState() == PlayState::Stoped)
                instance->updateFromPrefab();
        }
    }

    void Prefab::addLiveInstance(PrefabInstance* instance)
    {
        mLiveInstances.emplace_back(instance);
    }

    void Prefab::removeLiveInstance(PrefabInstance* instance)
    {
        auto it = std::find(mLiveInstances.begin(), mLiveInstances.end(), instance);
        assert(it != mLiveInstances.end());
        mLiveInstances.erase(it);
    }

    Object* Prefab::findNode(Guid nodeId) const
    {
        for (const TRef<Entity>& entity : mEntities)
        {
            if (entity->getGuid() == nodeId)
                return entity.get();

            for (const TRef<Component>& component : entity->getAllComponents())
            {
                if (component->getGuid() == nodeId)
                    return component.get();
            }
        }
        return nullptr;
    }

    template <> Type* Reflection::createType<Prefab>()
    {
        auto c = new TClass<Prefab, Object, AllocateFrequency::Low>("Prefab");
        c->addProperty("Entities", &Prefab::mEntities, CopyPolicy::Deep);
        c->addProperty("Root Entity", &Prefab::mRootEntity);
        return c;
    }
}
