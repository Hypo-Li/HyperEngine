#pragma once
#include <cstdint>
#include <Core/Reflection/Reflection.h>

namespace hyp
{
    enum class MaterialDomain : uint8_t
    {
        Surface,
        Decal,
        Volume,
        Post_Process,
        User_Interface,

        Count,
    };

    template <> Type* Reflection::createType<MaterialDomain>();

    enum class BlendMode : uint8_t
    {
        Opaque,
        Masked,
        Translucent,
        Additive,
        Multiply,

        Count,
    };

    template <> Type* Reflection::createType<BlendMode>();

    enum class ShadingModel : uint8_t
    {
        Unlit,
        Standard,
        Clear_Coat,

        Count,
    };

    template <> Type* Reflection::createType<ShadingModel>();

    enum class MaterialParameterType : uint8_t
    {
        Bool,
        Int,
        Float,
        Float2,
        Float3,
        Float4,
        Texture_2D,
        Texture_2D_Array,
        Texture_3D,
        Texture_Cube,

        Count,
    };

    inline bool isTextureParameterType(MaterialParameterType type) noexcept
    {
        if (type == MaterialParameterType::Texture_2D ||
            type == MaterialParameterType::Texture_2D_Array ||
            type == MaterialParameterType::Texture_3D ||
            type == MaterialParameterType::Texture_Cube)
            return true;
        return false;
    }
}
