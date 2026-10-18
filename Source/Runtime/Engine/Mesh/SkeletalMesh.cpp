#include "SkeletalMesh.h"

namespace hyp
{
    REGISTER_CLASS(SkeletalMesh)

    template <> Type* Reflection::createType<SkeletalMesh>()
    {
        auto c = new TClass<SkeletalMesh, Mesh>("SkeletalMesh");
        c->setDisplayName("Skeletal Mesh");
        c->addProperty("Skeleton", &SkeletalMesh::mSkeleton);
        return c;
    }
}
