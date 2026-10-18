#include "PlaneCollider.h"

namespace hyp
{
    REGISTER_CLASS(PlaneCollider)

    PlaneCollider::PlaneCollider()
    {
    }

    template <> Type* Reflection::createType<PlaneCollider>()
    {
        auto c = new TClass<PlaneCollider, Collider>("PlaneCollider");
        c->setDisplayName("Plane Collider");
        c->addProperty("Normal", &PlaneCollider::mNormal);
        c->addProperty("Constant", &PlaneCollider::mConstant);
        c->addProperty("Half Extent", &PlaneCollider::mHalfExtent);
        return c;
    }
}
