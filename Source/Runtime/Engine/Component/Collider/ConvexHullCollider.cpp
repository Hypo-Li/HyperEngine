#include "ConvexHullCollider.h"

namespace hyp
{
    REGISTER_CLASS(ConvexHullCollider)

    ConvexHullCollider::ConvexHullCollider()
    {
    }

    void ConvexHullCollider::onRegister()
    {
        if (!mHull)
            return;
        Collider::onRegister();
    }

    template <> Type* Reflection::createType<ConvexHullCollider>()
    {
        auto c = new TClass<ConvexHullCollider, Collider>("ConvexHullCollider");
        c->setDisplayName("Convex Hull Collider");
        c->addProperty("Hull", &ConvexHullCollider::mHull);
        return c;
    }
}
