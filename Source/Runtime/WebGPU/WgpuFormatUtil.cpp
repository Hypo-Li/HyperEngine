#include "WgpuFormatUtil.h"

#include <Core/Common/LogSystem.h>

namespace hyp::wgpu
{
    WGPUTextureFormat convertTextureFormat(rhi::TextureFormat format) noexcept
    {
        switch (format)
        {
        case rhi::TextureFormat::Undefined:          return WGPUTextureFormat_Undefined;
        case rhi::TextureFormat::R8_UNORM:           return WGPUTextureFormat_R8Unorm;
        case rhi::TextureFormat::R8_SNORM:           return WGPUTextureFormat_R8Snorm;
        case rhi::TextureFormat::R8_UINT:            return WGPUTextureFormat_R8Uint;
        case rhi::TextureFormat::R8_SINT:            return WGPUTextureFormat_R8Sint;
        case rhi::TextureFormat::RG8_UNORM:          return WGPUTextureFormat_RG8Unorm;
        case rhi::TextureFormat::RG8_SNORM:          return WGPUTextureFormat_RG8Snorm;
        case rhi::TextureFormat::RG8_UINT:           return WGPUTextureFormat_RG8Uint;
        case rhi::TextureFormat::RG8_SINT:           return WGPUTextureFormat_RG8Sint;
        case rhi::TextureFormat::RGBA8_UNORM:        return WGPUTextureFormat_RGBA8Unorm;
        case rhi::TextureFormat::RGBA8_SNORM:        return WGPUTextureFormat_RGBA8Snorm;
        case rhi::TextureFormat::RGBA8_UINT:         return WGPUTextureFormat_RGBA8Uint;
        case rhi::TextureFormat::RGBA8_SINT:         return WGPUTextureFormat_RGBA8Sint;
        case rhi::TextureFormat::RGBA8_SRGB:         return WGPUTextureFormat_RGBA8UnormSrgb;
        case rhi::TextureFormat::BGRA8_UNORM:        return WGPUTextureFormat_BGRA8Unorm;
        case rhi::TextureFormat::BGRA8_SRGB:         return WGPUTextureFormat_BGRA8UnormSrgb;
        case rhi::TextureFormat::A2BGR10_UNORM_PACK32: return WGPUTextureFormat_RGB10A2Unorm;
        case rhi::TextureFormat::B10G11R11_UFLOAT_PACK32: return WGPUTextureFormat_RG11B10Ufloat;
        case rhi::TextureFormat::E5B9G9R9_UFLOAT_PACK32: return WGPUTextureFormat_RGB9E5Ufloat;
        case rhi::TextureFormat::R16_UINT:           return WGPUTextureFormat_R16Uint;
        case rhi::TextureFormat::R16_SINT:           return WGPUTextureFormat_R16Sint;
        case rhi::TextureFormat::R16_SFLOAT:         return WGPUTextureFormat_R16Float;
        case rhi::TextureFormat::RG16_UINT:          return WGPUTextureFormat_RG16Uint;
        case rhi::TextureFormat::RG16_SINT:          return WGPUTextureFormat_RG16Sint;
        case rhi::TextureFormat::RG16_SFLOAT:        return WGPUTextureFormat_RG16Float;
        case rhi::TextureFormat::RGBA16_UINT:        return WGPUTextureFormat_RGBA16Uint;
        case rhi::TextureFormat::RGBA16_SINT:        return WGPUTextureFormat_RGBA16Sint;
        case rhi::TextureFormat::RGBA16_SFLOAT:      return WGPUTextureFormat_RGBA16Float;
        case rhi::TextureFormat::R32_UINT:           return WGPUTextureFormat_R32Uint;
        case rhi::TextureFormat::R32_SINT:           return WGPUTextureFormat_R32Sint;
        case rhi::TextureFormat::R32_SFLOAT:         return WGPUTextureFormat_R32Float;
        case rhi::TextureFormat::RG32_UINT:          return WGPUTextureFormat_RG32Uint;
        case rhi::TextureFormat::RG32_SINT:          return WGPUTextureFormat_RG32Sint;
        case rhi::TextureFormat::RG32_SFLOAT:        return WGPUTextureFormat_RG32Float;
        case rhi::TextureFormat::RGBA32_UINT:        return WGPUTextureFormat_RGBA32Uint;
        case rhi::TextureFormat::RGBA32_SINT:        return WGPUTextureFormat_RGBA32Sint;
        case rhi::TextureFormat::RGBA32_SFLOAT:      return WGPUTextureFormat_RGBA32Float;
        case rhi::TextureFormat::D16_UNORM:          return WGPUTextureFormat_Depth16Unorm;
        case rhi::TextureFormat::D32_SFLOAT:         return WGPUTextureFormat_Depth32Float;
        case rhi::TextureFormat::S8_UINT:            return WGPUTextureFormat_Stencil8;
        case rhi::TextureFormat::D24_UNORM_S8_UINT:  return WGPUTextureFormat_Depth24PlusStencil8;
        case rhi::TextureFormat::D32_SFLOAT_S8_UINT: return WGPUTextureFormat_Depth32FloatStencil8;
        case rhi::TextureFormat::BC1_RGB_UNORM:      return WGPUTextureFormat_BC1RGBAUnorm;
        case rhi::TextureFormat::BC1_RGB_SRGB:       return WGPUTextureFormat_BC1RGBAUnormSrgb;
        case rhi::TextureFormat::BC1_RGBA_UNORM:     return WGPUTextureFormat_BC1RGBAUnorm;
        case rhi::TextureFormat::BC1_RGBA_SRGB:      return WGPUTextureFormat_BC1RGBAUnormSrgb;
        case rhi::TextureFormat::BC2_UNORM:          return WGPUTextureFormat_BC2RGBAUnorm;
        case rhi::TextureFormat::BC2_SRGB:           return WGPUTextureFormat_BC2RGBAUnormSrgb;
        case rhi::TextureFormat::BC3_UNORM:          return WGPUTextureFormat_BC3RGBAUnorm;
        case rhi::TextureFormat::BC3_SRGB:           return WGPUTextureFormat_BC3RGBAUnormSrgb;
        case rhi::TextureFormat::BC4_UNORM:          return WGPUTextureFormat_BC4RUnorm;
        case rhi::TextureFormat::BC4_SNORM:          return WGPUTextureFormat_BC4RSnorm;
        case rhi::TextureFormat::BC5_UNORM:          return WGPUTextureFormat_BC5RGUnorm;
        case rhi::TextureFormat::BC5_SNORM:          return WGPUTextureFormat_BC5RGSnorm;
        case rhi::TextureFormat::BC6H_UFLOAT:        return WGPUTextureFormat_BC6HRGBUfloat;
        case rhi::TextureFormat::BC6H_SFLOAT:        return WGPUTextureFormat_BC6HRGBFloat;
        case rhi::TextureFormat::BC7_UNORM:          return WGPUTextureFormat_BC7RGBAUnorm;
        case rhi::TextureFormat::BC7_SRGB:           return WGPUTextureFormat_BC7RGBAUnormSrgb;
        default:
            LOG_ERROR("Unsupported WebGPU texture format");
            return WGPUTextureFormat_Undefined;
        }
    }

    WGPUTextureViewDimension convertTextureViewType(rhi::TextureViewType type) noexcept
    {
        switch (type)
        {
        case rhi::TextureViewType::Texture_View_2D:       return WGPUTextureViewDimension_2D;
        case rhi::TextureViewType::Texture_View_2D_Array: return WGPUTextureViewDimension_2DArray;
        case rhi::TextureViewType::Texture_View_3D:       return WGPUTextureViewDimension_3D;
        case rhi::TextureViewType::Texture_View_Cube:     return WGPUTextureViewDimension_Cube;
        default:                                          return WGPUTextureViewDimension_2D;
        }
    }
}
