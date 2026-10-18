#include "BoxCollider.h"

namespace hyp
{
    REGISTER_CLASS(BoxCollider)

    BoxCollider::BoxCollider()
    {
    }

    template <> Type* Reflection::createType<BoxCollider>()
    {
        auto c = new TClass<BoxCollider, Collider>("BoxCollider");
        c->setDisplayName("Box Collider");
        c->addProperty("Half Extent", &BoxCollider::mHalfExtent);
        return c;
    }
}
