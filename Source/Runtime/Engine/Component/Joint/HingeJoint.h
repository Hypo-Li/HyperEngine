#pragma once
#include "Joint.h"

namespace hyp
{
    // 对应 JPH::HingeConstraint——两个 Body 只留一根轴的相对转动自由度, 见门/轮子。
    class HingeJoint : public Joint
    {
        REFLECT_CLASS(HingeJoint)
    public:
        float3 getAnchor() const noexcept
        {
            return mAnchor;
        }

        void setAnchor(float3 anchor) noexcept
        {
            mAnchor = anchor;
        }

        float3 getConnectedAnchor() const noexcept
        {
            return mConnectedAnchor;
        }

        void setConnectedAnchor(float3 anchor) noexcept
        {
            mConnectedAnchor = anchor;
        }

        // 同一个方向在 anchor/connectedAnchor 各自局部空间下的表示, 见 HingeConstraintDesc。
        float3 getAxis() const noexcept
        {
            return mAxis;
        }

        void setAxis(float3 axis) noexcept
        {
            mAxis = axis;
        }

        // 角度, 取值范围 [-180, 0]——跟 Transform::getLocalRotationEuler() 一致, 对外的角度类
        // 字段用角度, 弧度只留给 HingeConstraintDesc 那种直接对应 Jolt 参数的桥接结构。
        float getMinAngle() const noexcept
        {
            return mMinAngle;
        }

        void setMinAngle(float angle) noexcept
        {
            mMinAngle = angle;
        }

        // 角度, 取值范围 [0, 180]。
        float getMaxAngle() const noexcept
        {
            return mMaxAngle;
        }

        void setMaxAngle(float angle) noexcept
        {
            mMaxAngle = angle;
        }

    protected:
        ConstraintDesc getConstraintDesc() const override
        {
            return HingeConstraintDesc{ mAnchor, mConnectedAnchor, mAxis, glm::radians(mMinAngle), glm::radians(mMaxAngle) };
        }

    private:
        float3 mAnchor = float3(0.0f);
        float3 mConnectedAnchor = float3(0.0f);
        float3 mAxis = float3(0.0f, 0.0f, 1.0f);
        float mMinAngle = -180.0f;
        float mMaxAngle = 180.0f;
    };

    template <> Type* Reflection::createType<HingeJoint>();
}
