#include "PhysicsTypes.h"

namespace hyp
{
    template <> Type* Reflection::createType<MotionType>()
    {
        auto e = new TEnumeration<MotionType>("MotionType");
        e->addValue("Static", MotionType::Static);
        e->addValue("Dynamic", MotionType::Dynamic);
        e->addValue("Kinematic", MotionType::Kinematic);
        return e;
    }
}
