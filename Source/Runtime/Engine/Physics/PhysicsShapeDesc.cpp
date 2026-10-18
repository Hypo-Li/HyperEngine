#include "PhysicsShapeDesc.h"

namespace hyp
{
    template <> Type* Reflection::createType<BoxShapeDesc>()
    {
        auto s = new TStructure<BoxShapeDesc>("BoxShapeDesc");
        s->addProperty("Half Extent", &BoxShapeDesc::halfExtent);
        return s;
    }

    template <> Type* Reflection::createType<SphereShapeDesc>()
    {
        auto s = new TStructure<SphereShapeDesc>("SphereShapeDesc");
        s->addProperty("Radius", &SphereShapeDesc::radius);
        return s;
    }

    template <> Type* Reflection::createType<CapsuleShapeDesc>()
    {
        auto s = new TStructure<CapsuleShapeDesc>("CapsuleShapeDesc");
        s->addProperty("Half Height", &CapsuleShapeDesc::halfHeight);
        s->addProperty("Radius", &CapsuleShapeDesc::radius);
        return s;
    }

    template <> Type* Reflection::createType<CylinderShapeDesc>()
    {
        auto s = new TStructure<CylinderShapeDesc>("CylinderShapeDesc");
        s->addProperty("Half Height", &CylinderShapeDesc::halfHeight);
        s->addProperty("Radius", &CylinderShapeDesc::radius);
        return s;
    }

    template <> Type* Reflection::createType<ConvexHullShapeDesc>()
    {
        auto s = new TStructure<ConvexHullShapeDesc>("ConvexHullShapeDesc");
        s->addProperty("Hull", &ConvexHullShapeDesc::hull);
        return s;
    }

    template <> Type* Reflection::createType<MeshShapeDesc>()
    {
        auto s = new TStructure<MeshShapeDesc>("MeshShapeDesc");
        s->addProperty("Mesh", &MeshShapeDesc::mesh);
        s->addProperty("LOD Level", &MeshShapeDesc::lodLevel);
        return s;
    }
}
