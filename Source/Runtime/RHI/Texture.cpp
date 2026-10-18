#include "Texture.h"
#include <Core/Common/LogSystem.h>

namespace hyp::rhi
{
    TextureView::~TextureView()
    {
        mTexture.decViewCount();
    }

    TextureAspect getFormatAspect(TextureFormat format)
    {
        if (isDepthFormat(format))
            return TextureAspect::Depth;
        if (isStencilFormat(format))
            return TextureAspect::Stencil;
        if (isDepthStencilFormat(format))
            return TextureAspect::Depth | TextureAspect::Stencil;
        else
            return TextureAspect::Color;
    }

    TextureViewDesc makeTextureDefaultViewDesc(const TextureDesc& textureDesc)
    {
        TextureViewDesc viewDesc;
        if (textureDesc.type == TextureType::Texture_2D)
        {
            if (textureDesc.layerCount == 1)
                viewDesc.type = TextureViewType::Texture_View_2D;
            else
                viewDesc.type = TextureViewType::Texture_View_2D_Array;
        }
        else if (textureDesc.type == TextureType::Texture_2D_Array)
        {
            viewDesc.type = TextureViewType::Texture_View_2D_Array;
        }
        else if (textureDesc.type == TextureType::Texture_3D)
        {
            viewDesc.type = TextureViewType::Texture_View_3D;
        }
        else if (textureDesc.type == TextureType::Texture_Cube)
        {
            assert(textureDesc.layerCount == 6);
            viewDesc.type = TextureViewType::Texture_View_Cube;
        }
        viewDesc.format = textureDesc.format;
        viewDesc.range.aspect = getFormatAspect(textureDesc.format);
        viewDesc.range.baseMip = 0;
        viewDesc.range.mipCount = textureDesc.mipCount;
        viewDesc.range.baseLayer = 0;
        viewDesc.range.layerCount = textureDesc.layerCount;
        return viewDesc;
    }

    TextureFormatBlockInfo getTextureFormatBlockInfo(rhi::TextureFormat format) noexcept
    {
        switch (format)
        {
        case rhi::TextureFormat::R8_UNORM:
        case rhi::TextureFormat::R8_SNORM:
        case rhi::TextureFormat::R8_UINT:
        case rhi::TextureFormat::R8_SINT:
        case rhi::TextureFormat::R8_SRGB:
            return { 1, 1, 1 };

        case rhi::TextureFormat::RG8_UNORM:
        case rhi::TextureFormat::RG8_SNORM:
        case rhi::TextureFormat::RG8_UINT:
        case rhi::TextureFormat::RG8_SINT:
        case rhi::TextureFormat::RG8_SRGB:
        case rhi::TextureFormat::R16_UNORM:
        case rhi::TextureFormat::R16_SNORM:
        case rhi::TextureFormat::R16_UINT:
        case rhi::TextureFormat::R16_SINT:
        case rhi::TextureFormat::R16_SFLOAT:
            return { 1, 1, 2 };


        case rhi::TextureFormat::RGBA8_UNORM:
        case rhi::TextureFormat::RGBA8_SNORM:
        case rhi::TextureFormat::RGBA8_UINT:
        case rhi::TextureFormat::RGBA8_SINT:
        case rhi::TextureFormat::RGBA8_SRGB:
        case rhi::TextureFormat::BGRA8_UNORM:
        case rhi::TextureFormat::BGRA8_SNORM:
        case rhi::TextureFormat::BGRA8_UINT:
        case rhi::TextureFormat::BGRA8_SINT:
        case rhi::TextureFormat::BGRA8_SRGB:
        case rhi::TextureFormat::A2RGB10_UNORM_PACK32:
        case rhi::TextureFormat::A2BGR10_UNORM_PACK32:
        case rhi::TextureFormat::RG16_UNORM:
        case rhi::TextureFormat::RG16_SNORM:
        case rhi::TextureFormat::RG16_UINT:
        case rhi::TextureFormat::RG16_SINT:
        case rhi::TextureFormat::RG16_SFLOAT:
        case rhi::TextureFormat::R32_UINT:
        case rhi::TextureFormat::R32_SINT:
        case rhi::TextureFormat::R32_SFLOAT:
            return { 1, 1, 4 };

        case rhi::TextureFormat::RGBA16_UNORM:
        case rhi::TextureFormat::RGBA16_SNORM:
        case rhi::TextureFormat::RGBA16_UINT:
        case rhi::TextureFormat::RGBA16_SINT:
        case rhi::TextureFormat::RGBA16_SFLOAT:
        case rhi::TextureFormat::RG32_UINT:
        case rhi::TextureFormat::RG32_SINT:
        case rhi::TextureFormat::RG32_SFLOAT:
            return { 1, 1, 8 };

        case rhi::TextureFormat::RGBA32_UINT:
        case rhi::TextureFormat::RGBA32_SINT:
        case rhi::TextureFormat::RGBA32_SFLOAT:
            return { 1, 1, 16 };

        case rhi::TextureFormat::BC1_RGB_UNORM:
        case rhi::TextureFormat::BC1_RGB_SRGB:
        case rhi::TextureFormat::BC1_RGBA_UNORM:
        case rhi::TextureFormat::BC1_RGBA_SRGB:
        case rhi::TextureFormat::BC4_UNORM:
        case rhi::TextureFormat::BC4_SNORM:
            return { 4, 4, 8 };

        case rhi::TextureFormat::BC2_UNORM:
        case rhi::TextureFormat::BC2_SRGB:
        case rhi::TextureFormat::BC3_UNORM:
        case rhi::TextureFormat::BC3_SRGB:
        case rhi::TextureFormat::BC5_UNORM:
        case rhi::TextureFormat::BC5_SNORM:
        case rhi::TextureFormat::BC6H_UFLOAT:
        case rhi::TextureFormat::BC6H_SFLOAT:
        case rhi::TextureFormat::BC7_UNORM:
        case rhi::TextureFormat::BC7_SRGB:
            return { 4, 4, 16 };

        default:
            LOG_ERROR("Unsupported texture format");
            return { 0, 0, 0 };
        }
    }
}
