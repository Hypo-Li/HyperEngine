#pragma once
#include "PhysicsLayer.h"
#include "PhysicsScene.h"

#include <Core/Common/Guid.h>

#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <functional>
#include <unordered_map>

namespace hyp
{
    // 进程级的 Jolt 引导层(Factory/类型系统/TempAllocator/JobSystemThreadPool), 一个进程只
    // 应该有一个。每个 hyp::Scene 各自的物理世界由 PhysicsScene 表示, 通过这里 createScene()
    // 出来, 共享同一份 TempAllocator/JobSystem。
    //
    // Mesh Shape 缓存也放在这一层, 不按 PhysicsScene 各存一份——JPH::Shape 不属于任何一个
    // JPH::PhysicsSystem, 能被不同 Scene 的 Body 共用。
    class PhysicsSystem
    {
        friend class PhysicsScene;
    public:
        PhysicsSystem();

        ~PhysicsSystem();

        void initialize(uint32_t maxThreads = 4, uint32_t tempAllocatorSize = 10 * 1024 * 1024);

        void shutdown();

        std::shared_ptr<PhysicsScene> createScene();

        void destroyScene(std::shared_ptr<PhysicsScene> scene);

        void invalidateMeshShapeCache(Guid meshGuid) noexcept;

        const PhysicsLayerSettings& getLayerSettings() const noexcept
        {
            return mLayerSettings;
        }

        // 此时不能有存活的 PhysicsScene, 它们的过滤器直接引用关系表。
        void setLayerSettings(PhysicsLayerSettings layerSettings);

        const PhysicsLayerRelationTable& getLayerRelations() const noexcept
        {
            return mLayerRelations;
        }

        // 不存在返回 -1。
        int32_t findLayerSlot(HString layerName) const;

    private:
        struct MeshShapeCacheKey
        {
            Guid meshGuid;
            uint32_t lodLevel = 0;

            bool operator==(const MeshShapeCacheKey&) const noexcept = default;
        };

        struct MeshShapeCacheKeyHash
        {
            size_t operator()(const MeshShapeCacheKey& key) const noexcept
            {
                return std::hash<Guid>()(key.meshGuid) ^ (std::hash<uint32_t>()(key.lodLevel) << 1);
            }
        };

        // buildFn 只在缓存未命中时调用一次。
        JPH::RefConst<JPH::Shape> getOrBuildMeshShape(Guid meshGuid, uint32_t lodLevel, const std::function<JPH::RefConst<JPH::Shape>()>& buildFn);

    private:
        std::unique_ptr<JPH::TempAllocatorImpl> mTempAllocator;
        std::unique_ptr<JPH::JobSystemThreadPool> mJobSystem;

        PhysicsLayerSettings mLayerSettings;
        std::unordered_map<HString, uint32_t> mLayerSlotByName;
        PhysicsLayerRelationTable mLayerRelations;

        std::vector<std::shared_ptr<PhysicsScene>> mScenes;
        std::unordered_map<MeshShapeCacheKey, JPH::RefConst<JPH::Shape>, MeshShapeCacheKeyHash> mMeshShapeCache;
        bool mIsInitialized = false;
    };
}
