#include "PhysicsScene.h"
#include "PhysicsSystem.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>

namespace hyp
{
    PhysicsScene::PhysicsScene()
    {
    }

    PhysicsScene::~PhysicsScene()
    {
    }

    void PhysicsScene::setGravity(const JPH::Vec3& gravity)
    {
        mJoltSystem->SetGravity(gravity);
    }

    JPH::BodyID PhysicsScene::createBody(const BodySettings& bodySetting)
    {
        JPH::BodyInterface& bodyInterface = mJoltSystem->GetBodyInterface();

        JPH::EMotionType motionType = JPH::EMotionType::Static;
        switch (bodySetting.motionType)
        {
        case MotionType::Static:
            motionType = JPH::EMotionType::Static;
            break;
        case MotionType::Kinematic:
            motionType = JPH::EMotionType::Kinematic;
            break;
        case MotionType::Dynamic:
            motionType = JPH::EMotionType::Dynamic;
            break;
        default:
            break;
        }

        JPH::Vec3 position = JPH::Vec3(bodySetting.position.x, bodySetting.position.y, bodySetting.position.z);
        JPH::Quat rotation = JPH::Quat(bodySetting.rotation.x, bodySetting.rotation.y, bodySetting.rotation.z, bodySetting.rotation.w);

        JPH::BodyCreationSettings creationSettings(
            bodySetting.shape,
            position,
            rotation,
            motionType,
            bodySetting.layer
        );

        if (bodySetting.motionType != MotionType::Static)
        {
            // 计算质量和惯性
            creationSettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateMassAndInertia;
            creationSettings.mMassPropertiesOverride.mMass = bodySetting.mass;
        }

        JPH::Body* body = bodyInterface.CreateBody(creationSettings);
        if (!body)
            return JPH::BodyID();

        if (bodySetting.isTrigger)
        {
            body->SetIsSensor(true);
        }

        JPH::BodyID bodyID = body->GetID();

        bodyInterface.AddBody(bodyID, bodySetting.motionType == MotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);

        return bodyID;
    }

    void PhysicsScene::destroyBody(JPH::BodyID bodyID)
    {
        if (bodyID.IsInvalid())
            return;

        JPH::BodyInterface& bodyInterface = mJoltSystem->GetBodyInterface();
        bodyInterface.RemoveBody(bodyID);
        bodyInterface.DestroyBody(bodyID);
    }

    void PhysicsScene::update(float deltaTime, int collisionSteps, JPH::TempAllocator* tempAllocator, JPH::JobSystem* jobSystem)
    {
        mJoltSystem->Update(deltaTime, collisionSteps, tempAllocator, jobSystem);
    }

    void PhysicsScene::initialize()
    {
        mBroadPhaseLayerInterface = std::make_unique<MyBroadPhaseLayerInterface>();
        mObjectVsBroadPhaseLayerFilter = std::make_unique<MyObjectVsBroadPhaseLayerFilter>();
        mObjectLayerPairFilter = std::make_unique<MyObjectLayerPairFilter>();

        const uint32_t maxBodies = 10000;
        const uint32_t numBodyMutexes = 0; // auto
        const uint32_t maxBodyPairs = 10000;
        const uint32_t maxContactConstraints = 10000;

        mJoltSystem->Init(maxBodies, numBodyMutexes, maxBodyPairs, maxContactConstraints, *mBroadPhaseLayerInterface, *mObjectVsBroadPhaseLayerFilter, *mObjectLayerPairFilter);
    }

}
