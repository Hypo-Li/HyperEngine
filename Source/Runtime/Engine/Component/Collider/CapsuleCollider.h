#pragma once
#include "Collider.h"

namespace hyp
{
    class CapsuleCollider : public Collider
    {
        REFLECT_CLASS(CapsuleCollider)
    public:
        CapsuleCollider();

        // 圆柱段的半高, 不含两端半球——跟 Jolt 的 CapsuleShapeSettings 参数含义一致。
        void setHalfHeight(float halfHeight) noexcept
        {
            mHalfHeight = halfHeight;
        }

        float getHalfHeight() const noexcept
        {
            return mHalfHeight;
        }

        void setRadius(float radius) noexcept
        {
            mRadius = radius;
        }

        float getRadius() const noexcept
        {
            return mRadius;
        }

    protected:
        ShapeDesc getShapeDesc() const override
        {
            return CapsuleShapeDesc{ mHalfHeight, mRadius };
        }

    private:
        float mHalfHeight = 0.5f;
        float mRadius = 0.5f;
    };

    template <> Type* Reflection::createType<CapsuleCollider>();
}
