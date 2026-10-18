#include "CompoundCollider.h"

namespace hyp
{
    REGISTER_CLASS(CompoundCollider)

    CompoundCollider::CompoundCollider()
    {
    }

    void CompoundCollider::onRegister()
    {
        if (!mCompoundCollision || mCompoundCollision->isEmpty())
            return;
        Collider::onRegister();
    }

    template <> Type* Reflection::createType<CompoundCollider>()
    {
        auto c = new TClass<CompoundCollider, Collider>("CompoundCollider");
        c->setDisplayName("Compound Collider");
        c->addProperty("Compound Collision", &CompoundCollider::mCompoundCollision);
        return c;
    }
}
