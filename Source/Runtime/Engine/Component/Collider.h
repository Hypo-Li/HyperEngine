#pragma once
#include "../Framework/Component.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

namespace hyp
{
    class Collider : public Component
    {
    public:
        Collider() = default;

    private:
        virtual JPH::RefConst<JPH::Shape> getJoltShape() = 0;

    private:
        bool mIsTrigger = false;
    };
}
