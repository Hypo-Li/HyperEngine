#pragma once
#include "../../Framework/Component.h"
#include "../../Physics/PhysicsLayer.h"
#include "../../Physics/PhysicsTypes.h"
#include "../../Physics/PhysicsMaterial.h"

#include <vector>

namespace hyp
{
    class Collider;
    class PhysicsScene;

    class ITriggerListener
    {
    public:
        virtual void onTriggerBegin(Collider& self, Collider& other) = 0;

        virtual void onTriggerEnd(Collider& self, Collider& other) = 0;

    protected:
        ~ITriggerListener() = default;
    };

    // 各种具体碰撞体的公共基类——本身不持有形状数据, 由派生类(BoxCollider 等)根据自己的
    // 参数生成 ShapeDesc, 不直接调用任何物理后端的 API。isTrigger 没勾选时自动就是刚体,
    // 具体运动方式由 mMotionType 决定(Static/Dynamic/Kinematic), 两者是正交的属性, 不需要
    // 一个单独的 RigidBody 组件配对。
    class Collider : public Component
    {
        friend class Scene;
        REFLECT_CLASS(Collider)
    public:
        Collider() = default;

        bool isTrigger() const noexcept
        {
            return mIsTrigger;
        }

        void setTrigger(bool trigger) noexcept
        {
            mIsTrigger = trigger;
        }

        HString getLayer() const noexcept
        {
            return mLayer;
        }

        void setLayer(HString layer) noexcept
        {
            mLayer = layer;
        }

        MotionType getMotionType() const noexcept
        {
            return mMotionType;
        }

        void setMotionType(MotionType motionType) noexcept
        {
            mMotionType = motionType;
        }

        // 只在 Dynamic 且 isOverrideMass() 为 true 时生效。
        float getMass() const noexcept
        {
            return mMass;
        }

        void setMass(float mass) noexcept
        {
            mMass = mass;
        }

        // false(默认)时质量按 PhysicsMaterial::density x 形状体积自动算; true 时改用 getMass()。
        bool isOverrideMass() const noexcept
        {
            return mOverrideMass;
        }

        void setOverrideMass(bool overrideMass) noexcept
        {
            mOverrideMass = overrideMass;
        }

        // 相对 mCenter(即 Shape 自己的局部原点, 不是 Entity 原点)的偏移, 只在 Dynamic 且
        // isOverrideCenterOfMass() 为 true 时生效。
        float3 getCenterOfMassOffset() const noexcept
        {
            return mCenterOfMassOffset;
        }

        void setCenterOfMassOffset(float3 offset) noexcept
        {
            mCenterOfMassOffset = offset;
        }

        // false(默认)时质心由 Jolt 按 Shape 自动算; true 时在此基础上叠加 getCenterOfMassOffset()。
        bool isOverrideCenterOfMass() const noexcept
        {
            return mOverrideCenterOfMass;
        }

        void setOverrideCenterOfMass(bool overrideCenterOfMass) noexcept
        {
            mOverrideCenterOfMass = overrideCenterOfMass;
        }

        PhysicsMaterial* getPhysicsMaterial() const noexcept
        {
            return mPhysicsMaterial;
        }

        void setPhysicsMaterial(PhysicsMaterial* material) noexcept
        {
            mPhysicsMaterial = material;
        }

        // 碰撞体中心相对 Entity 原点的局部偏移(跟 Unity/Unreal 的 Collider Center 是同一个
        // 概念)——形状本身(比如 BoxCollider 的 HalfExtent)是围绕这个中心定义的, 不是围绕
        // Entity 原点。
        float3 getCenter() const noexcept
        {
            return mCenter;
        }

        void setCenter(float3 center) noexcept
        {
            mCenter = center;
        }

        // mCenter 按 Entity 世界旋转转到世界空间后的世界坐标——跟 onRegister() 里建物理体用的
        // 是同一个公式, 不额外乘 scale(跟现有物理体摆放行为保持一致)
        double3 getWorldCenter() const noexcept;

        virtual bool supportsDynamicMotion() const noexcept
        {
            return true;
        }

        PhysicsBodyHandle getBodyHandle() const noexcept
        {
            return mBodyHandle;
        }

        void onRegister() override;

        void onUnregister() override;

        void onTick(float deltaTime) override;

        void onEndPlay() override;

        // 下面这批在编辑模式(没有物理世界)、Body 不存在或不是 Dynamic 时都是空操作。
        void addForce(float3 force);

        void addForceAtPosition(float3 force, double3 worldPosition);

        // 力和作用点都在 Entity 局部空间, 调用时就换算成世界空间。
        void addForceAtLocalPosition(float3 localForce, float3 localPosition);

        void addTorque(float3 torque);

        void addImpulse(float3 impulse, bool velocityChange = false);

        void addImpulseAtPosition(float3 impulse, double3 worldPosition);

        void addRadialImpulse(double3 origin, float radius, float strength, PhysicsRadialFalloff falloff, bool velocityChange = false);

        void setLinearVelocity(float3 velocity);

        float3 getLinearVelocity() const;

        void setAngularVelocity(float3 velocity);

        float3 getAngularVelocity() const;

        // 触发体和被它检测到的 Collider 两侧都会收到回调。
        void addTriggerListener(ITriggerListener* listener);

        void removeTriggerListener(ITriggerListener* listener);

    protected:
        virtual ShapeDesc getShapeDesc() const = 0;

    private:
        void notifyOverlap(Collider& other, bool begin);

        PhysicsScene* getBodyPhysicsScene() const noexcept;

        bool mIsTrigger = false;
        HString mLayer = PhysicsLayerSettings::getDefaultLayerName();
        MotionType mMotionType = MotionType::Static;
        float mMass = 1.0f;
        bool mOverrideMass = false;
        float3 mCenterOfMassOffset = float3(0.0f);
        bool mOverrideCenterOfMass = false;
        TRef<PhysicsMaterial> mPhysicsMaterial;
        float3 mCenter = float3(0.0f);

        PhysicsBodyHandle mBodyHandle;

        std::vector<ITriggerListener*> mTriggerListeners;
    };

    template <> Type* Reflection::createType<Collider>();
}
