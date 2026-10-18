#include "CompoundCollision.h"

namespace hyp
{
    REGISTER_CLASS(CompoundCollision)

    template <> Type* Reflection::createType<CompoundCollision::SubShapeTransform>()
    {
        auto s = new TStructure<CompoundCollision::SubShapeTransform>("CompoundCollision.SubShapeTransform");
        s->addProperty("Local Position", &CompoundCollision::SubShapeTransform::localPosition);
        s->addProperty("Local Rotation", &CompoundCollision::SubShapeTransform::localRotation);
        s->addProperty("Local Scale", &CompoundCollision::SubShapeTransform::localScale);
        return s;
    }

    template <> Type* Reflection::createType<CompoundCollision::BoxEntry>()
    {
        auto s = new TStructure<CompoundCollision::BoxEntry>("CompoundCollision.BoxEntry");
        s->addProperty("Shape", &CompoundCollision::BoxEntry::shape);
        s->addProperty("Transform", &CompoundCollision::BoxEntry::transform);
        return s;
    }

    template <> Type* Reflection::createType<CompoundCollision::SphereEntry>()
    {
        auto s = new TStructure<CompoundCollision::SphereEntry>("CompoundCollision.SphereEntry");
        s->addProperty("Shape", &CompoundCollision::SphereEntry::shape);
        s->addProperty("Transform", &CompoundCollision::SphereEntry::transform);
        return s;
    }

    template <> Type* Reflection::createType<CompoundCollision::CapsuleEntry>()
    {
        auto s = new TStructure<CompoundCollision::CapsuleEntry>("CompoundCollision.CapsuleEntry");
        s->addProperty("Shape", &CompoundCollision::CapsuleEntry::shape);
        s->addProperty("Transform", &CompoundCollision::CapsuleEntry::transform);
        return s;
    }

    template <> Type* Reflection::createType<CompoundCollision::CylinderEntry>()
    {
        auto s = new TStructure<CompoundCollision::CylinderEntry>("CompoundCollision.CylinderEntry");
        s->addProperty("Shape", &CompoundCollision::CylinderEntry::shape);
        s->addProperty("Transform", &CompoundCollision::CylinderEntry::transform);
        return s;
    }

    template <> Type* Reflection::createType<CompoundCollision::ConvexHullEntry>()
    {
        auto s = new TStructure<CompoundCollision::ConvexHullEntry>("CompoundCollision.ConvexHullEntry");
        s->addProperty("Shape", &CompoundCollision::ConvexHullEntry::shape);
        s->addProperty("Transform", &CompoundCollision::ConvexHullEntry::transform);
        return s;
    }

    template <> Type* Reflection::createType<CompoundCollision::MeshEntry>()
    {
        auto s = new TStructure<CompoundCollision::MeshEntry>("CompoundCollision.MeshEntry");
        s->addProperty("Shape", &CompoundCollision::MeshEntry::shape);
        s->addProperty("Transform", &CompoundCollision::MeshEntry::transform);
        return s;
    }

    template <> Type* Reflection::createType<CompoundCollision>()
    {
        auto c = new TClass<CompoundCollision>("CompoundCollision");
        c->setDisplayName("Compound Collision");
        c->addProperty("Boxes", &CompoundCollision::mBoxes);
        c->addProperty("Spheres", &CompoundCollision::mSpheres);
        c->addProperty("Capsules", &CompoundCollision::mCapsules);
        c->addProperty("Cylinders", &CompoundCollision::mCylinders);
        c->addProperty("Convex Hulls", &CompoundCollision::mConvexHulls);
        c->addProperty("Meshes", &CompoundCollision::mMeshes);
        return c;
    }
}
