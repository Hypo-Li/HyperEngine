#pragma once
#include "Collider.h"

namespace hyp
{
    class SphereCollider : public Collider
    {
        REFLECT_CLASS(SphereCollider)
    public:
        SphereCollider();

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
            return SphereShapeDesc{ mRadius };
        }

    private:
        float mRadius = 0.5f;
    };

    template <> Type* Reflection::createType<SphereCollider>();
}
