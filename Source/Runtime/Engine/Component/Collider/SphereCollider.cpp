#include "SphereCollider.h"

namespace hyp
{
    REGISTER_CLASS(SphereCollider)

    SphereCollider::SphereCollider()
    {
    }

    template <> Type* Reflection::createType<SphereCollider>()
    {
        auto c = new TClass<SphereCollider, Collider>("SphereCollider");
        c->setDisplayName("Sphere Collider");
        c->addProperty("Radius", &SphereCollider::mRadius);
        return c;
    }
}
