#pragma once
#include <Core/Object/Object.h>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>

namespace hyp
{
    class CollisionShape : public Object
    {
    public:
        CollisionShape()
        {
            JPH::BoxShapeSettings settings(JPH::Vec3(0.5f, 0.5f, 0.5f));
            mShape = settings.Create().Get();
        }

        JPH::RefConst<JPH::Shape> getJoltShape() const noexcept
        {
            return mShape;
        }

    protected:
        JPH::Ref<JPH::Shape> mShape;
    };
}
