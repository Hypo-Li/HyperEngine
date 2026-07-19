#include "VkFormatUtil.h"

namespace hyp::vk
{
    VkFormat hyp::vk::convertToVkFormat(rhi::TextureFormat format)
    {
        switch (format)
        {
        case rhi::TextureFormat::Undefined:
            return VK_FORMAT_UNDEFINED;
        case rhi::TextureFormat::R8_UNORM:
            return VK_FORMAT_R8_UNORM;
        case rhi::TextureFormat::R8_SNORM:
            return VK_FORMAT_R8_SNORM;
        case rhi::TextureFormat::R8_UINT:
            return VK_FORMAT_R8_UINT;
        case rhi::TextureFormat::R8_SINT:
            return VK_FORMAT_R8_SINT;
        case rhi::TextureFormat::R8_SRGB:
            return VK_FORMAT_R8_SRGB;
        case rhi::TextureFormat::RG8_UNORM:
            return VK_FORMAT_R8G8_UNORM;
        case rhi::TextureFormat::RG8_SNORM:
            return VK_FORMAT_R8G8_SNORM;
        case rhi::TextureFormat::RG8_UINT:
            return VK_FORMAT_R8G8_UINT;
        case rhi::TextureFormat::RG8_SINT:
            return VK_FORMAT_R8G8_SINT;
        case rhi::TextureFormat::RG8_SRGB:
            return VK_FORMAT_R8G8_SRGB;
        case rhi::TextureFormat::RGBA8_UNORM:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case rhi::TextureFormat::RGBA8_SNORM:
            return VK_FORMAT_R8G8B8A8_SNORM;
        case rhi::TextureFormat::RGBA8_UINT:
            return VK_FORMAT_R8G8B8A8_UINT;
        case rhi::TextureFormat::RGBA8_SINT:
            return VK_FORMAT_R8G8B8A8_SINT;
        case rhi::TextureFormat::RGBA8_SRGB:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case rhi::TextureFormat::BGRA8_UNORM:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case rhi::TextureFormat::BGRA8_SNORM:
            return VK_FORMAT_B8G8R8A8_SNORM;
        case rhi::TextureFormat::BGRA8_UINT:
            return VK_FORMAT_B8G8R8A8_UINT;
        case rhi::TextureFormat::BGRA8_SINT:
            return VK_FORMAT_B8G8R8A8_SINT;
        case rhi::TextureFormat::BGRA8_SRGB:
            return VK_FORMAT_B8G8R8A8_SRGB;
        case rhi::TextureFormat::A2RGB10_UNORM_PACK32:
            return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
        case rhi::TextureFormat::A2BGR10_UNORM_PACK32:
            return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case rhi::TextureFormat::B10G11R11_UFLOAT_PACK32:
            return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        case rhi::TextureFormat::E5B9G9R9_UFLOAT_PACK32:
            return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
        case rhi::TextureFormat::R16_UNORM:
            return VK_FORMAT_R16_UNORM;
        case rhi::TextureFormat::R16_SNORM:
            return VK_FORMAT_R16_SNORM;
        case rhi::TextureFormat::R16_UINT:
            return VK_FORMAT_R16_UINT;
        case rhi::TextureFormat::R16_SINT:
            return VK_FORMAT_R16_SINT;
        case rhi::TextureFormat::R16_SFLOAT:
            return VK_FORMAT_R16_SFLOAT;
        case rhi::TextureFormat::RG16_UNORM:
            return VK_FORMAT_R16G16_UNORM;
        case rhi::TextureFormat::RG16_SNORM:
            return VK_FORMAT_R16G16_SNORM;
        case rhi::TextureFormat::RG16_UINT:
            return VK_FORMAT_R16G16_UINT;
        case rhi::TextureFormat::RG16_SINT:
            return VK_FORMAT_R16G16_SINT;
        case rhi::TextureFormat::RG16_SFLOAT:
            return VK_FORMAT_R16G16_SFLOAT;
        case rhi::TextureFormat::RGBA16_UNORM:
            return VK_FORMAT_R16G16B16A16_UNORM;
        case rhi::TextureFormat::RGBA16_SNORM:
            return VK_FORMAT_R16G16B16A16_SNORM;
        case rhi::TextureFormat::RGBA16_UINT:
            return VK_FORMAT_R16G16B16A16_UINT;
        case rhi::TextureFormat::RGBA16_SINT:
            return VK_FORMAT_R16G16B16A16_SINT;
        case rhi::TextureFormat::RGBA16_SFLOAT:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case rhi::TextureFormat::R32_UINT:
            return VK_FORMAT_R32_UINT;
        case rhi::TextureFormat::R32_SINT:
            return VK_FORMAT_R32_SINT;
        case rhi::TextureFormat::R32_SFLOAT:
            return VK_FORMAT_R32_SFLOAT;
        case rhi::TextureFormat::RG32_UINT:
            return VK_FORMAT_R32G32_UINT;
        case rhi::TextureFormat::RG32_SINT:
            return VK_FORMAT_R32G32_SINT;
        case rhi::TextureFormat::RG32_SFLOAT:
            return VK_FORMAT_R32G32_SFLOAT;
        case rhi::TextureFormat::RGBA32_UINT:
            return VK_FORMAT_R32G32B32A32_UINT;
        case rhi::TextureFormat::RGBA32_SINT:
            return VK_FORMAT_R32G32B32A32_SINT;
        case rhi::TextureFormat::RGBA32_SFLOAT:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case rhi::TextureFormat::D16_UNORM:
            return VK_FORMAT_D16_UNORM;
        case rhi::TextureFormat::D32_SFLOAT:
            return VK_FORMAT_D32_SFLOAT;
        case rhi::TextureFormat::S8_UINT:
            return VK_FORMAT_S8_UINT;
        case rhi::TextureFormat::D16_UNORM_S8_UINT:
            return VK_FORMAT_D16_UNORM_S8_UINT;
        case rhi::TextureFormat::D24_UNORM_S8_UINT:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        case rhi::TextureFormat::D32_SFLOAT_S8_UINT:
            return VK_FORMAT_D32_SFLOAT_S8_UINT;
        case rhi::TextureFormat::BC1_RGB_UNORM:
            return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        case rhi::TextureFormat::BC1_RGB_SRGB:
            return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
        case rhi::TextureFormat::BC1_RGBA_UNORM:
            return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case rhi::TextureFormat::BC1_RGBA_SRGB:
            return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        case rhi::TextureFormat::BC2_UNORM:
            return VK_FORMAT_BC2_UNORM_BLOCK;
        case rhi::TextureFormat::BC2_SRGB:
            return VK_FORMAT_BC2_SRGB_BLOCK;
        case rhi::TextureFormat::BC3_UNORM:
            return VK_FORMAT_BC3_UNORM_BLOCK;
        case rhi::TextureFormat::BC3_SRGB:
            return VK_FORMAT_BC3_SRGB_BLOCK;
        case rhi::TextureFormat::BC4_UNORM:
            return VK_FORMAT_BC4_UNORM_BLOCK;
        case rhi::TextureFormat::BC4_SNORM:
            return VK_FORMAT_BC4_SNORM_BLOCK;
        case rhi::TextureFormat::BC5_UNORM:
            return VK_FORMAT_BC5_UNORM_BLOCK;
        case rhi::TextureFormat::BC5_SNORM:
            return VK_FORMAT_BC5_SNORM_BLOCK;
        case rhi::TextureFormat::BC6H_UFLOAT:
            return VK_FORMAT_BC6H_UFLOAT_BLOCK;
        case rhi::TextureFormat::BC6H_SFLOAT:
            return VK_FORMAT_BC6H_SFLOAT_BLOCK;
        case rhi::TextureFormat::BC7_UNORM:
            return VK_FORMAT_BC7_UNORM_BLOCK;
        case rhi::TextureFormat::BC7_SRGB:
            return VK_FORMAT_BC7_SRGB_BLOCK;
        default:
            return VK_FORMAT_UNDEFINED;
        }
    }

    VkImageAspectFlags convertToVkImageAspectFlags(rhi::TextureAspect aspect)
    {
        VkImageAspectFlags result = 0;
        if (enumHasAnyFlags(aspect, rhi::TextureAspect::Color))
            result |= VK_IMAGE_ASPECT_COLOR_BIT;
        if (enumHasAnyFlags(aspect, rhi::TextureAspect::Depth))
            result |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if (enumHasAnyFlags(aspect, rhi::TextureAspect::Stencil))
            result |= VK_IMAGE_ASPECT_STENCIL_BIT;
        return result;
    }

}
