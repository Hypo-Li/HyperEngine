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
        Sky,

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
        Cloth,
        From_Material, // 每像素运行期决定, 代价是各 shading model 专属字段没法被编译器死代码消除

        Count,
    };

    template <> Type* Reflection::createType<ShadingModel>();

    const char* getShaderMacroName(MaterialDomain domain) noexcept;

    const char* getShaderMacroName(BlendMode blendMode) noexcept;

    const char* getShaderMacroName(ShadingModel shadingModel) noexcept;

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
        Font,

        Count,
    };

    template <> Type* Reflection::createType<MaterialParameterType>();

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
