#include "PhysicsMaterial.h"

namespace hyp
{
    REGISTER_CLASS(PhysicsMaterial)

    template <> Type* Reflection::createType<PhysicsMaterial::CombineMode>()
    {
        auto e = new TEnumeration<PhysicsMaterial::CombineMode>("PhysicsMaterial::CombineMode");
        e->addValue("Average", PhysicsMaterial::CombineMode::Average);
        e->addValue("Minimum", PhysicsMaterial::CombineMode::Minimum);
        e->addValue("Multiply", PhysicsMaterial::CombineMode::Multiply);
        e->addValue("Maximum", PhysicsMaterial::CombineMode::Maximum);
        return e;
    }

    template <> Type* Reflection::createType<PhysicsMaterial>()
    {
        auto c = new TClass<PhysicsMaterial>("PhysicsMaterial");
        c->setDisplayName("Physics Material");
        c->addProperty("Friction", &PhysicsMaterial::mFriction);
        c->addProperty("Restitution", &PhysicsMaterial::mRestitution);
        c->addProperty("Density", &PhysicsMaterial::mDensity);
        c->addProperty("Friction Combine", &PhysicsMaterial::mFrictionCombine);
        c->addProperty("Restitution Combine", &PhysicsMaterial::mRestitutionCombine);
        return c;
    }
}
