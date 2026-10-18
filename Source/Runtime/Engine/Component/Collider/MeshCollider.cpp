#include "MeshCollider.h"

namespace hyp
{
    REGISTER_CLASS(MeshCollider)

    MeshCollider::MeshCollider()
    {
    }

    void MeshCollider::onRegister()
    {
        if (!mMesh)
            return;
        Collider::onRegister();
    }

    template <> Type* Reflection::createType<MeshCollider>()
    {
        auto c = new TClass<MeshCollider, Collider>("MeshCollider");
        c->setDisplayName("Mesh Collider");
        c->addProperty("Mesh", &MeshCollider::mMesh);
        c->addProperty("LOD Level", &MeshCollider::mLODLevel);
        return c;
    }
}
