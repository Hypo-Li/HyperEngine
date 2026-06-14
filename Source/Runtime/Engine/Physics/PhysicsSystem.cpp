#include "PhysicsSystem.h"

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>

namespace hyp
{
    PhysicsSystem::PhysicsSystem()
    {
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

        mJobSystem.reset();
        mTempAllocator.reset();

        if (JPH::Factory::sInstance != nullptr)
        {
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }

        mIsInitialized = false;
    }

}
