#pragma once
#include "PhysicsScene.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>

namespace hyp
{
    static constexpr JPH::ObjectLayer kDefaultObjectLayer = 0;
    static constexpr JPH::BroadPhaseLayer kDefaultBroadPhaseLayer = JPH::BroadPhaseLayer(0);

    class MyObjectLayerPairFilter : public JPH::ObjectLayerPairFilter
    {
    public:
        virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::ObjectLayer inLayer2) const override
        {
            return true;
        }
    };

    class MyBroadPhaseLayerInterface : public JPH::BroadPhaseLayerInterface
    {
    public:
        virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
        {
            return kDefaultBroadPhaseLayer;
        }

        virtual JPH::uint GetNumBroadPhaseLayers() const override
        {
            return 1;
        }
    };

    class MyObjectVsBroadPhaseLayerFilter : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
        virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
        {
            return true;
        }
    };

    class PhysicsSystem
    {
    public:
        PhysicsSystem();

        ~PhysicsSystem();

        void initialize(uint32_t maxThreads = 4, uint32_t tempAllocatorSize = 10 * 1024 * 1024);

        void shutdown();

        std::shared_ptr<PhysicsScene> createScene();

        void destroyScene(std::shared_ptr<PhysicsScene> scene);

        void update(float deltaTime);

    private:
        std::unique_ptr<JPH::TempAllocatorImpl> mTempAllocator;
        std::unique_ptr<JPH::JobSystemThreadPool> mJobSystem;

        std::vector<std::shared_ptr<PhysicsScene>> mScenes;
        bool mIsInitialized = false;
    };
}
