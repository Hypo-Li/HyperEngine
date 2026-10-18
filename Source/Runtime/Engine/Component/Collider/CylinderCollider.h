#pragma once
#include "Collider.h"

namespace hyp
{
    class CylinderCollider : public Collider
    {
        REFLECT_CLASS(CylinderCollider)
    public:
        CylinderCollider();

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
            return CylinderShapeDesc{ mHalfHeight, mRadius };
        }

    private:
        float mHalfHeight = 0.5f;
        float mRadius = 0.5f;
    };

    template <> Type* Reflection::createType<CylinderCollider>();
}
