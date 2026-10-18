#include "CapsuleCollider.h"

namespace hyp
{
    REGISTER_CLASS(CapsuleCollider)

    CapsuleCollider::CapsuleCollider()
    {
    }

    template <> Type* Reflection::createType<CapsuleCollider>()
    {
        auto c = new TClass<CapsuleCollider, Collider>("CapsuleCollider");
        c->setDisplayName("Capsule Collider");
        c->addProperty("Half Height", &CapsuleCollider::mHalfHeight);
        c->addProperty("Radius", &CapsuleCollider::mRadius);
        return c;
    }
}
