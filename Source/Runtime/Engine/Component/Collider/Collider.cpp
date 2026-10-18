#include "Collider.h"
#include "../../Context.h"
#include "../../Framework/Entity.h"
#include "../../Framework/Scene.h"
#include "../../Framework/Transform.h"
#include "../../Physics/PhysicsScene.h"
#include "../../Physics/PhysicsSystem.h"

#include <Core/Common/LogSystem.h>

#include <algorithm>
#include <cassert>

namespace hyp
{
    REGISTER_CLASS(Collider)

    double3 Collider::getWorldCenter() const noexcept
    {
        Transform& transform = getEntity()->getTransform();
        return transform.getWorldPosition() + double3(transform.getWorldRotation() * mCenter);
    }

    void Collider::onRegister()
    {
        // Dynamic body 的位姿由模拟结果决定, 要放在物理步进之后才能读到这一帧的最新值;
        // Static/Kinematic 不依赖模拟结果(Kinematic 反而要在步进之前把 Transform 喂给物理
        // 世界), 放在物理步进之前。
        setTickGroup(mMotionType == MotionType::Dynamic ? TickGroup::Post_Physics : TickGroup::Pre_Physics);

        PhysicsScene* physicsScene = getScene()->getPhysicsScene();
        if (!physicsScene)
            return; // 编辑模式下没有 PhysicsScene——Play 时场景整份重新反序列化, 会再走一次 onRegister()

        Transform& transform = getEntity()->getTransform();

        int32_t layerSlot = Context::get().getPhysicsSystem().findLayerSlot(mLayer);
        if (layerSlot < 0)
        {
            LOG_WARN("Collider layer \"{}\" does not exist, using Default", mLayer.toCString());
            layerSlot = 0;
        }

        PhysicsScene::BodySettings settings;
        settings.position = getWorldCenter();
        settings.rotation = transform.getWorldRotation();
        settings.scale = transform.getWorldScale();
        settings.shape = getShapeDesc();
        settings.motionType = mMotionType;
        settings.isTrigger = mIsTrigger;
        settings.layer = static_cast<uint32_t>(layerSlot);
        settings.ownerId = getGuid();
        settings.mass = mMass;
        settings.overrideMass = mOverrideMass;
        settings.centerOfMassOffset = mCenterOfMassOffset;
        settings.overrideCenterOfMass = mOverrideCenterOfMass;
        settings.material = mPhysicsMaterial;

        mBodyHandle = physicsScene->createBody(settings);
    }

    void Collider::onUnregister()
    {
        if (PhysicsScene* physicsScene = getScene()->getPhysicsScene())
            physicsScene->destroyBody(mBodyHandle);
        mBodyHandle = PhysicsBodyHandle();
    }

    void Collider::onTick(float deltaTime)
    {
        if (!mBodyHandle.isValid())
            return;

        PhysicsScene* physicsScene = getScene()->getPhysicsScene();
        if (!physicsScene)
            return;

        Transform& transform = getEntity()->getTransform();

        if (mMotionType == MotionType::Dynamic)
        {
            // body 的世界位置是"中心"的位置, 不是 Entity 原点的位置——要把偏移(按刚读回来的
            // body 旋转转到世界空间)减回去, 才是该写进 Transform 的 Entity 原点位置。
            Quaternion bodyRotation = physicsScene->getBodyRotation(mBodyHandle);
            double3 bodyPosition = physicsScene->getBodyPosition(mBodyHandle);
            transform.setWorldRotation(bodyRotation);
            transform.setWorldPosition(bodyPosition - double3(bodyRotation * mCenter));
        }
        else if (mMotionType == MotionType::Kinematic)
        {
            physicsScene->setBodyTransform(mBodyHandle, getWorldCenter(), transform.getWorldRotation());
        }
    }

    void Collider::onEndPlay()
    {
        getScene()->endColliderOverlaps(this);
    }

    PhysicsScene* Collider::getBodyPhysicsScene() const noexcept
    {
        return mBodyHandle.isValid() ? getScene()->getPhysicsScene() : nullptr;
    }

    void Collider::addForce(float3 force)
    {
        if (PhysicsScene* physicsScene = getBodyPhysicsScene())
            physicsScene->addForce(mBodyHandle, force);
    }

    void Collider::addForceAtPosition(float3 force, double3 worldPosition)
    {
        if (PhysicsScene* physicsScene = getBodyPhysicsScene())
            physicsScene->addForceAtPosition(mBodyHandle, force, worldPosition);
    }

    void Collider::addForceAtLocalPosition(float3 localForce, float3 localPosition)
    {
        PhysicsScene* physicsScene = getBodyPhysicsScene();
        if (!physicsScene)
            return;

        const double4x4& worldMatrix = getEntity()->getTransform().getWorldMatrix();
        double3 worldPosition = double3(worldMatrix * double4(double3(localPosition), 1.0));
        float3 worldForce = float3(double3(worldMatrix * double4(double3(localForce), 0.0)));
        physicsScene->addForceAtPosition(mBodyHandle, worldForce, worldPosition);
    }

    void Collider::addTorque(float3 torque)
    {
        if (PhysicsScene* physicsScene = getBodyPhysicsScene())
            physicsScene->addTorque(mBodyHandle, torque);
    }

    void Collider::addImpulse(float3 impulse, bool velocityChange)
    {
        if (PhysicsScene* physicsScene = getBodyPhysicsScene())
            physicsScene->addImpulse(mBodyHandle, impulse, velocityChange);
    }

    void Collider::addImpulseAtPosition(float3 impulse, double3 worldPosition)
    {
        if (PhysicsScene* physicsScene = getBodyPhysicsScene())
            physicsScene->addImpulseAtPosition(mBodyHandle, impulse, worldPosition);
    }

    void Collider::addRadialImpulse(double3 origin, float radius, float strength, PhysicsRadialFalloff falloff, bool velocityChange)
    {
        if (PhysicsScene* physicsScene = getBodyPhysicsScene())
            physicsScene->addRadialImpulse(mBodyHandle, origin, radius, strength, falloff, velocityChange);
    }

    void Collider::setLinearVelocity(float3 velocity)
    {
        if (PhysicsScene* physicsScene = getBodyPhysicsScene())
            physicsScene->setLinearVelocity(mBodyHandle, velocity);
    }

    float3 Collider::getLinearVelocity() const
    {
        PhysicsScene* physicsScene = getBodyPhysicsScene();
        return physicsScene ? physicsScene->getLinearVelocity(mBodyHandle) : float3(0.0f);
    }

    void Collider::setAngularVelocity(float3 velocity)
    {
        if (PhysicsScene* physicsScene = getBodyPhysicsScene())
            physicsScene->setAngularVelocity(mBodyHandle, velocity);
    }

    float3 Collider::getAngularVelocity() const
    {
        PhysicsScene* physicsScene = getBodyPhysicsScene();
        return physicsScene ? physicsScene->getAngularVelocity(mBodyHandle) : float3(0.0f);
    }

    void Collider::addTriggerListener(ITriggerListener* listener)
    {
        assert(std::find(mTriggerListeners.begin(), mTriggerListeners.end(), listener) == mTriggerListeners.end());
        mTriggerListeners.push_back(listener);
    }

    void Collider::removeTriggerListener(ITriggerListener* listener)
    {
        auto it = std::find(mTriggerListeners.begin(), mTriggerListeners.end(), listener);
        assert(it != mTriggerListeners.end());
        mTriggerListeners.erase(it);
    }

    void Collider::notifyOverlap(Collider& other, bool begin)
    {
        // 回调里可能增删监听者, 遍历快照并在每次调用前确认它还在。
        std::vector<ITriggerListener*> listeners = mTriggerListeners;
        for (ITriggerListener* listener : listeners)
        {
            if (std::find(mTriggerListeners.begin(), mTriggerListeners.end(), listener) == mTriggerListeners.end())
                continue;

            if (begin)
                listener->onTriggerBegin(*this, other);
            else
                listener->onTriggerEnd(*this, other);
        }
    }

    template <> Type* Reflection::createType<Collider>()
    {
        auto c = new TClass<Collider, Component>("Collider");
        c->addProperty("Is Trigger", &Collider::mIsTrigger);
        c->addProperty("Layer", &Collider::mLayer);
        c->addProperty("Motion Type", &Collider::mMotionType);
        c->addProperty("Override Mass", &Collider::mOverrideMass);
        c->addProperty("Mass", &Collider::mMass);
        c->addProperty("Override Center Of Mass", &Collider::mOverrideCenterOfMass);
        c->addProperty("Center Of Mass Offset", &Collider::mCenterOfMassOffset);
        c->addProperty("Physics Material", &Collider::mPhysicsMaterial);
        c->addProperty("Center", &Collider::mCenter);
        return c;
    }
}
