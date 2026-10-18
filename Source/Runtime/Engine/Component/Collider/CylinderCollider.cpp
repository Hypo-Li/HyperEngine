#include "CylinderCollider.h"

namespace hyp
{
    REGISTER_CLASS(CylinderCollider)

    CylinderCollider::CylinderCollider()
    {
    }

    template <> Type* Reflection::createType<CylinderCollider>()
    {
        auto c = new TClass<CylinderCollider, Collider>("CylinderCollider");
        c->setDisplayName("Cylinder Collider");
        c->addProperty("Half Height", &CylinderCollider::mHalfHeight);
        c->addProperty("Radius", &CylinderCollider::mRadius);
        return c;
    }
}
