#include "MaterialTypes.h"

#include <cassert>

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
        e->addValue("Sky", MaterialDomain::Sky);
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
        e->addValue("Cloth", ShadingModel::Cloth);
        e->addValue("From Material", ShadingModel::From_Material);
        return e;
    }

    const char* getShaderMacroName(MaterialDomain domain) noexcept
    {
        switch (domain)
        {
        case MaterialDomain::Surface: return "MATERIAL_DOMAIN_SURFACE";
        case MaterialDomain::Decal: return "MATERIAL_DOMAIN_DECAL";
        case MaterialDomain::Volume: return "MATERIAL_DOMAIN_VOLUME";
        case MaterialDomain::Post_Process: return "MATERIAL_DOMAIN_POST_PROCESS";
        case MaterialDomain::User_Interface: return "MATERIAL_DOMAIN_USER_INTERFACE";
        case MaterialDomain::Sky: return "MATERIAL_DOMAIN_SKY";
        case MaterialDomain::Count: break;
        }
        assert(false && "Invalid MaterialDomain");
        return "MATERIAL_DOMAIN_SURFACE";
    }

    const char* getShaderMacroName(BlendMode blendMode) noexcept
    {
        switch (blendMode)
        {
        case BlendMode::Opaque: return "BLEND_MODE_OPAQUE";
        case BlendMode::Masked: return "BLEND_MODE_MASKED";
        case BlendMode::Translucent: return "BLEND_MODE_TRANSLUCENT";
        case BlendMode::Additive: return "BLEND_MODE_ADDITIVE";
        case BlendMode::Multiply: return "BLEND_MODE_MULTIPLY";
        case BlendMode::Count: break;
        }
        assert(false && "Invalid BlendMode");
        return "BLEND_MODE_OPAQUE";
    }

    const char* getShaderMacroName(ShadingModel shadingModel) noexcept
    {
        switch (shadingModel)
        {
        case ShadingModel::Unlit: return "SHADING_MODEL_UNLIT";
        case ShadingModel::Standard: return "SHADING_MODEL_STANDARD";
        case ShadingModel::Clear_Coat: return "SHADING_MODEL_CLEAR_COAT";
        case ShadingModel::Cloth: return "SHADING_MODEL_CLOTH";
        case ShadingModel::From_Material: return "SHADING_MODEL_FROM_MATERIAL";
        case ShadingModel::Count: break;
        }
        assert(false && "Invalid ShadingModel");
        return "SHADING_MODEL_STANDARD";
    }

    template <> Type* Reflection::createType<MaterialParameterType>()
    {
        auto e = new TEnumeration<MaterialParameterType>("MaterialParameterType");
        e->addValue("Bool", MaterialParameterType::Bool);
        e->addValue("Int", MaterialParameterType::Int);
        e->addValue("Float", MaterialParameterType::Float);
        e->addValue("Float2", MaterialParameterType::Float2);
        e->addValue("Float3", MaterialParameterType::Float3);
        e->addValue("Float4", MaterialParameterType::Float4);
        e->addValue("Texture 2D", MaterialParameterType::Texture_2D);
        e->addValue("Texture 2D Array", MaterialParameterType::Texture_2D_Array);
        e->addValue("Texture 3D", MaterialParameterType::Texture_3D);
        e->addValue("Texture Cube", MaterialParameterType::Texture_Cube);
        e->addValue("Font", MaterialParameterType::Font);
        return e;
    }
}
