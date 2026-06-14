#pragma once
#include "Collider.h"
#include <Core/Math/Vector.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>

namespace hyp
{
    class BoxCollider : public Collider
    {
    public:
        BoxCollider();

        void setHalfExtent(float3 halfExtent)
        {
            mHalfExtent = halfExtent;

            // 根据场景是否正在Play, 以及是否已经创建Shape
            // 决定是否设置Shape
        }

        float3 getHalfExtent() const noexcept
        {
            return mHalfExtent;
        }

        JPH::RefConst<JPH::Shape> getJoltShape() override
        {
            if (!mShape)
            {
                JPH::BoxShapeSettings settings(JPH::Vec3(mHalfExtent.x, mHalfExtent.y, mHalfExtent.z));
                mShape = settings.Create().Get();
            }
            return mShape;
        }

    private:
        float3 mHalfExtent;
        float3 mOffset;

        JPH::ShapeRefC mShape;
    };
}
