#include "PhysicsSystem.h"

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>

#include <algorithm>
#include <cassert>

namespace hyp
{
    PhysicsSystem::PhysicsSystem()
    {
        setLayerSettings({});
    }

    PhysicsSystem::~PhysicsSystem()
    {
        shutdown();
    }

    void PhysicsSystem::initialize(uint32_t maxThreads, uint32_t tempAllocatorSize)
    {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        mTempAllocator = std::make_unique<JPH::TempAllocatorImpl>(tempAllocatorSize);
        mJobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, maxThreads);

        mIsInitialized = true;
    }

    void PhysicsSystem::shutdown()
    {
        if (!mIsInitialized)
            return;

        mScenes.clear();
        // 持有的 JPH::RefConst<Shape> 得在 Factory 销毁前释放, 跟 mScenes.clear() 顺序一致。
        mMeshShapeCache.clear();

        mJobSystem.reset();
        mTempAllocator.reset();

        if (JPH::Factory::sInstance != nullptr)
        {
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }

        mIsInitialized = false;
    }

    std::shared_ptr<PhysicsScene> PhysicsSystem::createScene()
    {
        assert(mIsInitialized);

        auto scene = std::make_shared<PhysicsScene>();
        scene->initialize(*mTempAllocator, *mJobSystem, *this);
        mScenes.push_back(scene);
        return scene;
    }

    void PhysicsSystem::destroyScene(std::shared_ptr<PhysicsScene> scene)
    {
        auto it = std::find(mScenes.begin(), mScenes.end(), scene);
        assert(it != mScenes.end());
        mScenes.erase(it);
    }

    void PhysicsSystem::setLayerSettings(PhysicsLayerSettings layerSettings)
    {
        assert(mScenes.empty());

        mLayerSettings = std::move(layerSettings);

        mLayerSlotByName.clear();
        mLayerSlotByName.emplace(PhysicsLayerSettings::getDefaultLayerName(), 0);
        for (size_t i = 0; i < mLayerSettings.layerNames.size(); ++i)
            mLayerSlotByName.emplace(mLayerSettings.layerNames[i], static_cast<uint32_t>(i + 1));

        for (auto& row : mLayerRelations)
            row.fill(PhysicsLayerRelation::Block);

        for (const PhysicsLayerRelationEntry& entry : mLayerSettings.relations)
        {
            int32_t slotA = findLayerSlot(entry.a);
            int32_t slotB = findLayerSlot(entry.b);
            if (slotA < 0 || slotB < 0)
                continue;

            mLayerRelations[slotA][slotB] = entry.relation;
            mLayerRelations[slotB][slotA] = entry.relation;
        }
    }

    int32_t PhysicsSystem::findLayerSlot(HString layerName) const
    {
        auto it = mLayerSlotByName.find(layerName);
        return it != mLayerSlotByName.end() ? static_cast<int32_t>(it->second) : -1;
    }

    void PhysicsSystem::invalidateMeshShapeCache(Guid meshGuid) noexcept
    {
        for (auto it = mMeshShapeCache.begin(); it != mMeshShapeCache.end();)
        {
            if (it->first.meshGuid == meshGuid)
                it = mMeshShapeCache.erase(it);
            else
                ++it;
        }
    }

    JPH::RefConst<JPH::Shape> PhysicsSystem::getOrBuildMeshShape(Guid meshGuid, uint32_t lodLevel, const std::function<JPH::RefConst<JPH::Shape>()>& buildFn)
    {
        MeshShapeCacheKey key{ meshGuid, lodLevel };
        auto it = mMeshShapeCache.find(key);
        if (it != mMeshShapeCache.end())
            return it->second;

        JPH::RefConst<JPH::Shape> shape = buildFn();
        mMeshShapeCache.emplace(key, shape);
        return shape;
    }

}
