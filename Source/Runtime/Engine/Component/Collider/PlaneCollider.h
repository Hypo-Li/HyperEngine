#pragma once
#include "Collider.h"

namespace hyp
{
    // 负半空间(x·normal + constant < 0)是实体。
    class PlaneCollider : public Collider
    {
        REFLECT_CLASS(PlaneCollider)
    public:
        PlaneCollider();

        void setNormal(float3 normal) noexcept
        {
            mNormal = normal;
        }

        float3 getNormal() const noexcept
        {
            return mNormal;
        }

        void setConstant(float constant) noexcept
        {
            mConstant = constant;
        }

        float getConstant() const noexcept
        {
            return mConstant;
        }

        // broad phase 用的包围盒半径, 不是平面的真实边界。
        void setHalfExtent(float halfExtent) noexcept
        {
            mHalfExtent = halfExtent;
        }

        float getHalfExtent() const noexcept
        {
            return mHalfExtent;
        }

        bool supportsDynamicMotion() const noexcept override
        {
            return false;
        }

    protected:
        ShapeDesc getShapeDesc() const override
        {
            return PlaneShapeDesc{ mNormal, mConstant, mHalfExtent };
        }

    private:
        float3 mNormal = float3(0.0f, 0.0f, 1.0f);
        float mConstant = 0.0f;
        float mHalfExtent = 1000.0f;
    };

    template <> Type* Reflection::createType<PlaneCollider>();
}
