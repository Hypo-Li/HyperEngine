#pragma once
#include "Collider.h"

namespace hyp
{
    class BoxCollider : public Collider
    {
        REFLECT_CLASS(BoxCollider)
    public:
        BoxCollider();

        void setHalfExtent(float3 halfExtent) noexcept
        {
            mHalfExtent = halfExtent;
        }

        float3 getHalfExtent() const noexcept
        {
            return mHalfExtent;
        }

    protected:
        ShapeDesc getShapeDesc() const override
        {
            return BoxShapeDesc{ mHalfExtent };
        }

    private:
        float3 mHalfExtent = float3(0.5f);
    };

    template <> Type* Reflection::createType<BoxCollider>();
}
