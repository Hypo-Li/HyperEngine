#pragma once
#include "../../Framework/Component.h"
#include "../../Physics/PhysicsTypes.h"
#include "../Collider/Collider.h"
#include <Core/Common/SoftRef.h>

namespace hyp
{
    // 两个物体之间应该保持什么样的相对运动关系(铰链/弹簧/焊死……), 跟 Collider("一个物体长
    // 什么形状")是正交的另一套组件——具体类型见派生类(HingeJoint 等)。
    class Joint : public Component
    {
        REFLECT_CLASS(Joint)
    public:
        Joint() = default;

        ~Joint() noexcept = default;

        void onRegister() override;

        void onUnregister() override;

        void onTick(float deltaTime) override;

        Collider* getCollider() const noexcept
        {
            return mCollider.resolve();
        }

        // bodyA, 必须显式指定——一个 Entity 可能挂多个 Collider, 没有"猜"的余地。
        void setCollider(Collider* collider) noexcept
        {
            mCollider = TSoftRef<Collider>(collider);
        }

        Collider* getConnectedCollider() const noexcept
        {
            return mConnectedCollider.resolve();
        }

        // bodyB, 空 = 连接静态世界。
        void setConnectedCollider(Collider* collider) noexcept
        {
            mConnectedCollider = TSoftRef<Collider>(collider);
        }

    protected:
        // 对称 Collider::getShapeDesc()。
        virtual ConstraintDesc getConstraintDesc() const = 0;

        PhysicsConstraintHandle getConstraintHandle() const noexcept
        {
            return mConstraintHandle;
        }

    private:
        TSoftRef<Collider> mCollider;
        TSoftRef<Collider> mConnectedCollider;
        PhysicsConstraintHandle mConstraintHandle;
    };

    template <> Type* Reflection::createType<Joint>();
}
