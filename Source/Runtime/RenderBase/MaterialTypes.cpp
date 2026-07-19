#include "MaterialTypes.h"

namespace hyp
{
    template <> Type* Reflection::createType<MaterialDomain>()
    {
        auto e = new TEnumeration<MaterialDomain>("MaterialDomain");
        e->addValue("Surface", MaterialDomain::Surface);
        e->addValue("Decal", MaterialDomain::Decal);
        e->addValue("Volume", MaterialDomain::Volume);
        e->addValue("Post Process", MaterialDomain::Post_Process);
        e->addValue("User Interface", MaterialDomain::User_Interface);
        return e;
    }

    template <> Type* Reflection::createType<BlendMode>()
    {
        auto e = new TEnumeration<BlendMode>("BlendMode");
        e->addValue("Opaque", BlendMode::Opaque);
        e->addValue("Masked", BlendMode::Masked);
        e->addValue("Translucent", BlendMode::Translucent);
        e->addValue("Additive", BlendMode::Additive);
        e->addValue("Multiply", BlendMode::Multiply);
        return e;
    }

    template <> Type* Reflection::createType<ShadingModel>()
    {
        auto e = new TEnumeration<ShadingModel>("ShadingModel");
        e->addValue("Unlit", ShadingModel::Unlit);
        e->addValue("Standard", ShadingModel::Standard);
        e->addValue("Clear Coat", ShadingModel::Clear_Coat);
        return e;
    }
}
