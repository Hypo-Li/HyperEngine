#pragma once
#include <Core/Math/Vector.h>
#include <Core/Math/Quaternion.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace hyp
{
    enum class MotionType
    {
        Static,
        Dynamic,
        Kinematic
    };

    class PhysicsSystem;
    class PhysicsScene
    {
        friend class PhysicsSystem;
    public:
        PhysicsScene();

        ~PhysicsScene();

        PhysicsScene(const PhysicsScene&) = delete;
        PhysicsScene& operator=(const PhysicsScene&) = delete;
        PhysicsScene(PhysicsScene&&) = delete;
        PhysicsScene& operator=(PhysicsScene&&) = delete;

        void setGravity(const JPH::Vec3& gravity);

        struct BodySettings
        {
            float3 position = float3(0.0f);
            Quaternion rotation = Quaternion(1, 0, 0, 0);
            JPH::RefConst<JPH::Shape> shape;
            JPH::ObjectLayer layer;
            MotionType motionType = MotionType::Static;
            bool isTrigger = false;
            float mass = 1.0f;
        };

        JPH::BodyID createBody(const BodySettings& bodySetting);

        void destroyBody(JPH::BodyID bodyID);

        void update(float deltaTime, int collisionSteps, JPH::TempAllocator* tempAllocator, JPH::JobSystem* jobSystem);

    private:
        void initialize();

    private:
        std::unique_ptr<JPH::PhysicsSystem> mJoltSystem;

        std::unique_ptr<JPH::BroadPhaseLayerInterface> mBroadPhaseLayerInterface;
        std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilter> mObjectVsBroadPhaseLayerFilter;
        std::unique_ptr<JPH::ObjectLayerPairFilter> mObjectLayerPairFilter;

    };
}
