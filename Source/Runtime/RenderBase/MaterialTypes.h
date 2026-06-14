#pragma once
#include <cstdint>

#include <Core/Common/EnumBitwise.h>

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

    enum class BlendMode : uint8_t
    {
        Opaque,
        Masked,
        Translucent,
        Additive,
        Multiply,

        Count,
    };

    enum class ShadingModel : uint8_t
    {
        Unlit,
        Standard,
        Clear_Coat,

        Count,
    };

    enum class MaterialParameterType : uint8_t
    {
        Bool,
        Int,
        Float,
        Float2,
        Float3,
        Float4,
        Texture2D,
        Texture2DArray,
        Texture3D,
        TextureCube,

        Count,
    };

    inline bool isTextureParameterType(MaterialParameterType type) noexcept
    {
        if (type == MaterialParameterType::Texture2D ||
            type == MaterialParameterType::Texture2DArray ||
            type == MaterialParameterType::Texture3D ||
            type == MaterialParameterType::TextureCube)
            return true;
        return false;
    }
}
