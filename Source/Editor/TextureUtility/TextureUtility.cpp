#include "TextureUtility.h"
#include "Mipmap.h"

#include <Core/Common/LogSystem.h>
#include <Core/Math/Math.h>

#include <ispc_texcomp.h>
#include <rgbcx.h>

#include <algorithm>
#include <cstring>
#include <execution>
#include <functional>
#include <mutex>

namespace hyp
{
    bool isCompressionFormat(rhi::TextureFormat format) noexcept
    {
        switch (format)
        {
        case rhi::TextureFormat::BC1_RGB_UNORM:
        case rhi::TextureFormat::BC1_RGB_SRGB:
        case rhi::TextureFormat::BC1_RGBA_UNORM:
        case rhi::TextureFormat::BC1_RGBA_SRGB:
        case rhi::TextureFormat::BC2_UNORM:
        case rhi::TextureFormat::BC2_SRGB:
        case rhi::TextureFormat::BC3_UNORM:
        case rhi::TextureFormat::BC3_SRGB:
        case rhi::TextureFormat::BC4_UNORM:
        case rhi::TextureFormat::BC4_SNORM:
        case rhi::TextureFormat::BC5_UNORM:
        case rhi::TextureFormat::BC5_SNORM:
        case rhi::TextureFormat::BC6H_UFLOAT:
        case rhi::TextureFormat::BC6H_SFLOAT:
        case rhi::TextureFormat::BC7_UNORM:
        case rhi::TextureFormat::BC7_SRGB:
            return true;
        default:
            return false;
        }
    }

    uint32_t getBlockSizeByFormat(rhi::TextureFormat format) noexcept
    {
        switch (format)
        {
        case rhi::TextureFormat::BC1_RGB_UNORM:
        case rhi::TextureFormat::BC1_RGB_SRGB:
        case rhi::TextureFormat::BC1_RGBA_UNORM:
        case rhi::TextureFormat::BC1_RGBA_SRGB:
        case rhi::TextureFormat::BC4_UNORM:
        case rhi::TextureFormat::BC4_SNORM:
            return 8;
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
            return 16;
        default:
            return 0;
        }
    }

    uint32_t getPixelFormatChannels(PixelFormat format) noexcept
    {
        switch (format)
        {
        case PixelFormat::R8_UNORM:
        case PixelFormat::R16_UNORM:
        case PixelFormat::R32_SFLOAT:
            return 1;
        case PixelFormat::RG8_UNORM:
        case PixelFormat::RG16_UNORM:
        case PixelFormat::RG16_SFLOAT:
        case PixelFormat::RG32_SFLOAT:
            return 2;
        case PixelFormat::RGB8_UNORM:
        case PixelFormat::RGB16_UNORM:
        case PixelFormat::RGB16_SFLOAT:
        case PixelFormat::RGB32_SFLOAT:
            return 3;
        case PixelFormat::RGBA8_UNORM:
        case PixelFormat::RGBA16_UNORM:
        case PixelFormat::RGBA16_SFLOAT:
        case PixelFormat::RGBA32_SFLOAT:
            return 4;
        default:
            return 0;
        }
    }

    namespace
    {
        // 写入该像素格式单个通道"完全不透明"的原始字节(UNORM8→0xFF, UNORM16→0xFFFF,
        // SFLOAT16→0x3C00, SFLOAT32→1.0f 的比特), outBytes 需要至少 getPixelSizeByFormat(format)/
        // getPixelFormatChannels(format) 字节的空间
        void getOpaqueChannelBytes(PixelFormat format, uint8_t* outBytes) noexcept
        {
            switch (format)
            {
            case PixelFormat::R8_UNORM:
            case PixelFormat::RG8_UNORM:
            case PixelFormat::RGB8_UNORM:
            case PixelFormat::RGBA8_UNORM:
            {
                outBytes[0] = 0xFF;
                break;
            }
            case PixelFormat::R16_UNORM:
            case PixelFormat::RG16_UNORM:
            case PixelFormat::RGB16_UNORM:
            case PixelFormat::RGBA16_UNORM:
            {
                uint16_t value = 0xFFFF;
                std::memcpy(outBytes, &value, sizeof(value));
                break;
            }
            case PixelFormat::R16_SFLOAT:
            case PixelFormat::RG16_SFLOAT:
            case PixelFormat::RGB16_SFLOAT:
            case PixelFormat::RGBA16_SFLOAT:
            {
                uint16_t value = 0x3C00; // half 1.0
                std::memcpy(outBytes, &value, sizeof(value));
                break;
            }
            case PixelFormat::R32_SFLOAT:
            case PixelFormat::RG32_SFLOAT:
            case PixelFormat::RGB32_SFLOAT:
            case PixelFormat::RGBA32_SFLOAT:
            {
                float value = 1.0f;
                std::memcpy(outBytes, &value, sizeof(value));
                break;
            }
            default:
                break;
            }
        }

        // BC4/BC5 是单/双通道格式, 其余压缩格式统一按 4 通道紧凑排列
        uint32_t getCompressionInputChannels(rhi::TextureFormat format) noexcept
        {
            switch (format)
            {
            case rhi::TextureFormat::BC4_UNORM:
            case rhi::TextureFormat::BC4_SNORM:
                return 1;
            case rhi::TextureFormat::BC5_UNORM:
            case rhi::TextureFormat::BC5_SNORM:
                return 2;
            default:
                return 4;
            }
        }

        // 把源数据重新整理成宽高对齐到 4 的倍数、且通道数恰好是 dstChannels 的紧凑排列, 供
        // ispc_texcomp(BC6H/BC7)使用——dstChannels 可能比源通道数多(比如 3 通道 RGB 要 pad 到
        // 4 通道, 多出来的通道补该格式的不透明值), 也可能比源通道数少(比如 4 通道 RGBA 要砍到
        // 1/2 通道给 BC4/BC5 用, 多余的通道直接丢弃)。边缘(宽高不是 4 的倍数的部分)按 clamp 补,
        // 复制最后一行/列
        std::vector<uint8_t> generatePaddingMipData(
            uint32_t width,
            uint32_t height,
            uint32_t depth,
            uint32_t layers,
            PixelFormat format,
            uint32_t dstChannels,
            std::span<const uint8_t> srcData
        )
        {
            uint32_t pixelSize = getPixelSizeByFormat(format);
            uint32_t channels = getPixelFormatChannels(format);
            uint32_t channelSize = pixelSize / channels;
            uint32_t dstPixelSize = dstChannels * channelSize;
            // 目标通道数比源通道数少时(BC4/BC5 的情形)只能拷贝 dstChannels 份, 多了会越界写
            uint32_t copySize = std::min(channels, dstChannels) * channelSize;

            uint8_t opaqueChannelBytes[4] = {};
            if (dstChannels > channels)
                getOpaqueChannelBytes(format, opaqueChannelBytes);

            uint32_t paddedWidth = (width + 3) & ~3;
            uint32_t paddedHeight = (height + 3) & ~3;

            size_t paddedSliceSize = static_cast<size_t>(paddedWidth) * paddedHeight * dstPixelSize;
            std::vector<uint8_t> paddingPixels(paddedSliceSize * depth * layers);

            for (uint32_t layer = 0; layer < layers; ++layer)
            {
                for (uint32_t z = 0; z < depth; ++z)
                {
                    uint8_t* dstSlice = paddingPixels.data() + (layer * depth + z) * paddedSliceSize;
                    const uint8_t* srcSlice = srcData.data() + (layer * depth + z) * width * height * pixelSize;

                    for (uint32_t y = 0; y < paddedHeight; ++y)
                    {
                        // 垂直方向边缘处理：取原图最后一行
                        uint32_t srcY = std::min(y, height - 1);
                        const uint8_t* srcRow = srcSlice + srcY * width * pixelSize;
                        uint8_t* dstRow = dstSlice + y * paddedWidth * dstPixelSize;

                        for (uint32_t x = 0; x < paddedWidth; ++x)
                        {
                            // 水平方向边缘处理：取原图最后的一列像素
                            uint32_t srcX = std::min(x, width - 1);
                            const uint8_t* srcPixel = srcRow + srcX * pixelSize;
                            uint8_t* dstPixel = dstRow + x * dstPixelSize;

                            std::memcpy(dstPixel, srcPixel, copySize);
                            for (uint32_t c = channels; c < dstChannels; ++c)
                                std::memcpy(dstPixel + c * channelSize, opaqueChannelBytes, channelSize);
                        }
                    }
                }
            }

            return paddingPixels;
        }

        // rgbcx 的 encode_* 函数本身不做多线程, 压缩大图的并行度全靠这里的按块分发
        void forEachBlock(
            uint32_t width,
            uint32_t height,
            uint32_t channels,
            uint32_t blockOutputSize,
            std::span<const uint8_t> srcData,
            std::span<uint8_t> dstData,
            const std::function<void(const uint8_t* blockPixels, uint8_t* blockOutput)>& encodeBlock
        )
        {
            uint32_t blocksX = width / 4;
            uint32_t blocksY = height / 4;

            std::vector<uint32_t> blockIndices(static_cast<size_t>(blocksX) * blocksY);
            for (size_t i = 0; i < blockIndices.size(); ++i)
                blockIndices[i] = static_cast<uint32_t>(i);

            std::for_each(
                std::execution::par,
                blockIndices.begin(),
                blockIndices.end(),
                [&](uint32_t index)
                {
                    uint32_t bx = index % blocksX;
                    uint32_t by = index / blocksX;

                    uint8_t block[4 * 4 * 4];
                    for (uint32_t row = 0; row < 4; ++row)
                    {
                        const uint8_t* srcRow = srcData.data() + ((static_cast<size_t>(by) * 4 + row) * width + bx * 4) * channels;
                        std::memcpy(block + row * 4 * channels, srcRow, 4 * channels);
                    }

                    encodeBlock(block, dstData.data() + static_cast<size_t>(index) * blockOutputSize);
                }
            );
        }

        void compressWithRGBCX(
            rhi::TextureFormat format,
            uint32_t width,
            uint32_t height,
            std::span<const uint8_t> srcData,
            std::span<uint8_t> dstData
        )
        {
            static std::once_flag initFlag;
            std::call_once(initFlag, [] { rgbcx::init(); });

            uint32_t level = rgbcx::MAX_LEVEL;

            switch (format)
            {
            case rhi::TextureFormat::BC1_RGB_UNORM:
            case rhi::TextureFormat::BC1_RGB_SRGB:
            case rhi::TextureFormat::BC1_RGBA_UNORM:
            case rhi::TextureFormat::BC1_RGBA_SRGB:
            {
                forEachBlock(width, height, 4, 8, srcData, dstData,
                    [level](const uint8_t* px, uint8_t* dst) { rgbcx::encode_bc1(level, dst, px, /*allow_3color*/ true, /*use_transparent_texels_for_black*/ false); });
                break;
            }
            case rhi::TextureFormat::BC3_UNORM:
            case rhi::TextureFormat::BC3_SRGB:
            {
                forEachBlock(width, height, 4, 16, srcData, dstData,
                    [level](const uint8_t* px, uint8_t* dst) { rgbcx::encode_bc3(level, dst, px); });
                break;
            }
            case rhi::TextureFormat::BC4_UNORM:
            case rhi::TextureFormat::BC4_SNORM:
            {
                forEachBlock(width, height, 1, 8, srcData, dstData,
                    [](const uint8_t* px, uint8_t* dst) { rgbcx::encode_bc4(dst, px, /*stride*/ 1); });
                break;
            }
            case rhi::TextureFormat::BC5_UNORM:
            case rhi::TextureFormat::BC5_SNORM:
            {
                forEachBlock(width, height, 2, 16, srcData, dstData,
                    [](const uint8_t* px, uint8_t* dst) { rgbcx::encode_bc5(dst, px, 0, 1, /*stride*/ 2); });
                break;
            }
            default:
                break;
            }
        }

        void compressTextureData(
            uint32_t width,
            uint32_t height,
            std::span<const uint8_t> srcData,
            std::span<uint8_t> dstData,
            rhi::TextureFormat compressionFormat,
            bool hasAlpha
        )
        {
            // srcData 已经由调用方(bakeMips(), 经 generatePaddingMipData() 整理)按这个格式实际
            // 需要的通道数紧凑排列好了, 这里只管按格式转发调用, 不做任何数据整形
            rgba_surface surface;
            surface.width = width;
            surface.height = height;
            surface.ptr = const_cast<uint8_t*>(srcData.data());
            surface.stride = static_cast<int32_t>(srcData.size() / height);

            switch (compressionFormat)
            {
            case rhi::TextureFormat::BC1_RGB_UNORM:
            case rhi::TextureFormat::BC1_RGB_SRGB:
            case rhi::TextureFormat::BC1_RGBA_UNORM:
            case rhi::TextureFormat::BC1_RGBA_SRGB:
            case rhi::TextureFormat::BC3_UNORM:
            case rhi::TextureFormat::BC3_SRGB:
            case rhi::TextureFormat::BC4_UNORM:
            case rhi::TextureFormat::BC4_SNORM:
            case rhi::TextureFormat::BC5_UNORM:
            case rhi::TextureFormat::BC5_SNORM:
            {
                compressWithRGBCX(compressionFormat, width, height, srcData, dstData);
                break;
            }
            case rhi::TextureFormat::BC6H_UFLOAT:
            case rhi::TextureFormat::BC6H_SFLOAT:
            {
                bc6h_enc_settings settings;
                GetProfile_bc6h_veryslow(&settings);
                CompressBlocksBC6H(&surface, dstData.data(), &settings);
                break;
            }
            case rhi::TextureFormat::BC7_UNORM:
            case rhi::TextureFormat::BC7_SRGB:
            {
                bc7_enc_settings settings;
                if (hasAlpha)
                    GetProfile_alpha_slow(&settings);
                else
                    GetProfile_slow(&settings);
                CompressBlocksBC7(&surface, dstData.data(), &settings);
                break;
            }
            default:
                break;
            }
        }

// 切线空间法线贴图专用的 decode/encode: 把前两个通道(X/Y)重建成完整单位法线参与重采样,
// 而不是像普通贴图那样逐通道分别线性平均——相邻纹素法线方向不一致时, 后者会让重采样出的向量
// 长度小于 1, shader 端用 sqrt(1-x²-y²) 重建 Z 时被放大成偏"平"的法线。PixelType 覆盖
// RG8_UNORM/RGB8_UNORM/RGBA8_UNORM 三种法线贴图常见导入格式(法线贴图源图往往是 3/4 通道
// PNG, 不能假设一定是 2 通道), X/Y 之外多出来的通道(B/A, 比如未裁剪掉的蓝通道)不参与法线
// 重建, 按普通逐通道线性重采样原样保留
// invokeGenerateNormalMap() 重采样时用来累加 x/y/z 与透传通道的中间值类型——MSVC 不允许
// 局部类定义友元函数, 所以放在函数外面
struct NormalTexel
{
    float3 xyz{ 0.0f };
    float2 extra{ 0.0f };

    NormalTexel() = default;
    // resample1D() 里 T(0) 累加器初始化要用到, 广播到 xyz/extra 的每个分量
    NormalTexel(float scalar) noexcept : xyz(scalar), extra(scalar) {}
    NormalTexel(float3 xyz_, float2 extra_) noexcept : xyz(xyz_), extra(extra_) {}

    friend NormalTexel operator+(const NormalTexel& a, const NormalTexel& b) noexcept
    {
        return NormalTexel{ a.xyz + b.xyz, a.extra + b.extra };
    }
    friend NormalTexel operator*(const NormalTexel& a, float s) noexcept
    {
        return NormalTexel{ a.xyz * s, a.extra * s };
    }
};

template <typename PixelType>
void invokeGenerateNormalMap(
    uint32_t srcWidth,
    uint32_t srcHeight,
    uint32_t srcDepth,
    std::span<const uint8_t> src,
    std::span<uint8_t> dstMips,
    uint32_t mipCount,
    MipmapFilter filter
)
{
    constexpr int channels = sizeof(PixelType);

    auto decode = [](PixelType v) -> NormalTexel
    {
        float x = v.x / 255.0f * 2.0f - 1.0f;
        float y = v.y / 255.0f * 2.0f - 1.0f;
        float z = std::sqrt(std::max(1.0f - x * x - y * y, 0.0f));
        NormalTexel t;
        t.xyz = float3(x, y, z);
        if constexpr (channels >= 3) t.extra.x = v.z / 255.0f;
        if constexpr (channels >= 4) t.extra.y = v.w / 255.0f;
        return t;
    };
    auto encode = [](NormalTexel t) -> PixelType
    {
        float3 n = Math::safeNormalize(t.xyz);
        PixelType v;
        v.x = static_cast<uint8_t>(std::clamp(n.x * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);
        v.y = static_cast<uint8_t>(std::clamp(n.y * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);
        if constexpr (channels >= 3) v.z = static_cast<uint8_t>(std::clamp(t.extra.x, 0.0f, 1.0f) * 255.0f + 0.5f);
        if constexpr (channels >= 4) v.w = static_cast<uint8_t>(std::clamp(t.extra.y, 0.0f, 1.0f) * 255.0f + 0.5f);
        return v;
    };

    auto typedSrc = std::span<const PixelType>(reinterpret_cast<const PixelType*>(src.data()), src.size() / sizeof(PixelType));
    auto typedDst = std::span<PixelType>(reinterpret_cast<PixelType*>(dstMips.data()), dstMips.size() / sizeof(PixelType));

    generateMipmap<PixelType>(srcWidth, srcHeight, srcDepth, typedSrc, typedDst, mipCount, filter, decode, encode);
}

#define INVOKE_GENERATE(FORMAT, IS_SRGB) \
    invokeGenerate<PixelFormat::FORMAT, IS_SRGB>(srcWidth, srcHeight, srcDepth, srcData, dstMips, mipCount, filter)

#define CASE_FORMAT(FORMAT) \
    case PixelFormat::FORMAT: \
        INVOKE_GENERATE(FORMAT, false); \
        break;

#define CASE_FORMAT_SRGB(FORMAT) \
    case PixelFormat::FORMAT: \
        if (isSRGB) INVOKE_GENERATE(FORMAT, true); \
        else INVOKE_GENERATE(FORMAT, false); \
        break;

        void dispatchGenerate(
            PixelFormat format,
            bool isSRGB,
            CompressionPreset compressionPreset,
            uint32_t srcWidth,
            uint32_t srcHeight,
            uint32_t srcDepth,
            std::span<const uint8_t> srcData,
            std::span<uint8_t> dstMips,
            uint32_t mipCount,
            MipmapFilter filter
        )
        {
            if (compressionPreset == CompressionPreset::Normal_Map)
            {
                switch (format)
                {
                case PixelFormat::RG8_UNORM:
                    invokeGenerateNormalMap<TVector2<uint8_t>>(srcWidth, srcHeight, srcDepth, srcData, dstMips, mipCount, filter);
                    return;
                case PixelFormat::RGB8_UNORM:
                    invokeGenerateNormalMap<TVector3<uint8_t>>(srcWidth, srcHeight, srcDepth, srcData, dstMips, mipCount, filter);
                    return;
                case PixelFormat::RGBA8_UNORM:
                    invokeGenerateNormalMap<TVector4<uint8_t>>(srcWidth, srcHeight, srcDepth, srcData, dstMips, mipCount, filter);
                    return;
                default:
                    break;
                }
            }

            switch (format)
            {
            CASE_FORMAT_SRGB(R8_UNORM)
            CASE_FORMAT_SRGB(RG8_UNORM)
            CASE_FORMAT_SRGB(RGB8_UNORM)
            CASE_FORMAT_SRGB(RGBA8_UNORM)
            CASE_FORMAT(R16_UNORM)
            CASE_FORMAT(RG16_UNORM)
            CASE_FORMAT(RGB16_UNORM)
            CASE_FORMAT(RGBA16_UNORM)
            CASE_FORMAT(R16_SFLOAT)
            CASE_FORMAT(RG16_SFLOAT)
            CASE_FORMAT(RGB16_SFLOAT)
            CASE_FORMAT(RGBA16_SFLOAT)
            CASE_FORMAT(R32_SFLOAT)
            CASE_FORMAT(RG32_SFLOAT)
            CASE_FORMAT(RGB32_SFLOAT)
            CASE_FORMAT(RGBA32_SFLOAT)
            default: break;
            }
        }

#undef INVOKE_GENERATE
#undef CASE_FORMAT
#undef CASE_FORMAT_SRGB

        template <PixelFormat F, bool isSRGB>
        void invokeGenerateCube(
            uint32_t srcWidth,
            uint32_t srcHeight,
            std::span<const uint8_t> src,
            std::span<uint8_t> dstMips,
            uint32_t mipCount,
            MipmapFilter filter
        )
        {
            using Traits = PixelFormatTraits<F, isSRGB>;
            using T = typename Traits::Type;

            auto typedSrc = std::span<const T>(reinterpret_cast<const T*>(src.data()), src.size() / sizeof(T));
            auto typedDst = std::span<T>(reinterpret_cast<T*>(dstMips.data()), dstMips.size() / sizeof(T));

            generateMipmapCube<T>(
                srcWidth, srcHeight,
                typedSrc, typedDst, mipCount,
                filter,
                Traits::getDecode(),
                Traits::getEncode()
            );
        }

#define INVOKE_GENERATE_CUBE(FORMAT, IS_SRGB) \
    invokeGenerateCube<PixelFormat::FORMAT, IS_SRGB>(srcWidth, srcHeight, srcData, dstMips, mipCount, filter)

#define CASE_FORMAT_CUBE(FORMAT) \
    case PixelFormat::FORMAT: INVOKE_GENERATE_CUBE(FORMAT, false); break;

#define CASE_FORMAT_CUBE_SRGB(FORMAT) \
    case PixelFormat::FORMAT: \
        if (isSRGB) INVOKE_GENERATE_CUBE(FORMAT, true); \
        else INVOKE_GENERATE_CUBE(FORMAT, false); \
        break;

        void dispatchGenerateCube(
            PixelFormat format,
            bool isSRGB,
            uint32_t srcWidth,
            uint32_t srcHeight,
            std::span<const uint8_t> srcData,
            std::span<uint8_t> dstMips,
            uint32_t mipCount,
            MipmapFilter filter
        )
        {
            switch (format)
            {
            CASE_FORMAT_CUBE_SRGB(R8_UNORM)
            CASE_FORMAT_CUBE_SRGB(RG8_UNORM)
            CASE_FORMAT_CUBE_SRGB(RGB8_UNORM)
            CASE_FORMAT_CUBE_SRGB(RGBA8_UNORM)
            CASE_FORMAT_CUBE(R16_UNORM)
            CASE_FORMAT_CUBE(RG16_UNORM)
            CASE_FORMAT_CUBE(RGB16_UNORM)
            CASE_FORMAT_CUBE(RGBA16_UNORM)
            CASE_FORMAT_CUBE(R16_SFLOAT)
            CASE_FORMAT_CUBE(RG16_SFLOAT)
            CASE_FORMAT_CUBE(RGB16_SFLOAT)
            CASE_FORMAT_CUBE(RGBA16_SFLOAT)
            CASE_FORMAT_CUBE(R32_SFLOAT)
            CASE_FORMAT_CUBE(RG32_SFLOAT)
            CASE_FORMAT_CUBE(RGB32_SFLOAT)
            CASE_FORMAT_CUBE(RGBA32_SFLOAT)
            default: break;
            }
        }

#undef INVOKE_GENERATE_CUBE
#undef CASE_FORMAT_CUBE
#undef CASE_FORMAT_CUBE_SRGB

        // 对应旧 Texture::generateMipmaps(): 从 mip0 出发, 用可分离重采样(2D/3D)或球面滤波
        // (cube)生成剩余各级 mip 的未压缩像素, 拼成一整块连续内存(不含 mip0 本身)
        std::vector<uint8_t> generateMipmapChain(std::span<const uint8_t> mip0Data, const Texture& texture)
        {
            const Texture::Description& description = texture.getDescription();
            std::vector<uint8_t> mipmaps;

            uint32_t pixelSize = getPixelSizeByFormat(description.format);
            uint32_t arrayLayers = description.arrayLayers;
            size_t mipmapSize = 0;
            {
                uint32_t mipWidth = description.width;
                uint32_t mipHeight = description.height;
                uint32_t mipDepth = description.depth;
                for (uint32_t i = 1; i < description.mipLevels; ++i)
                {
                    mipWidth = std::max(mipWidth >> 1, 1u);
                    mipHeight = std::max(mipHeight >> 1, 1u);
                    mipDepth = std::max(mipDepth >> 1, 1u);

                    mipmapSize += mipWidth * mipHeight * mipDepth * pixelSize * arrayLayers;
                }
            }
            mipmaps.resize(mipmapSize);

            uint32_t mipCount = description.mipLevels > 0 ? description.mipLevels - 1 : 0;
            MipmapFilter filter = MipmapFilter::Box;

            if (description.isCube)
            {
                size_t face0Size = static_cast<size_t>(description.width) * description.height * pixelSize;
                size_t cubeMipsSize = mipmapSize;
                assert(mipmapSize % 6 == 0);

                std::span<const uint8_t> srcCube = { mip0Data.data(), face0Size * 6 };
                std::span<uint8_t> dstCube = { mipmaps.data(), cubeMipsSize };
                dispatchGenerateCube(
                    description.format,
                    texture.isSRGB(),
                    description.width,
                    description.height,
                    srcCube,
                    dstCube,
                    mipCount,
                    filter
                );

                return mipmaps;
            }

            size_t layer0Size = static_cast<size_t>(description.width) * description.height * description.depth * pixelSize;
            size_t layerMipsSize = mipmapSize / arrayLayers;
            for (uint32_t layer = 0; layer < arrayLayers; ++layer)
            {
                std::span<const uint8_t> srcLayer = { mip0Data.data() + layer * layer0Size, layer0Size };
                std::span<uint8_t> dstLayer = { mipmaps.data() + layer * layerMipsSize, layerMipsSize };
                dispatchGenerate(
                    description.format,
                    texture.isSRGB(),
                    texture.getCompressionPreset(),
                    description.width,
                    description.height,
                    description.depth,
                    srcLayer,
                    dstLayer,
                    mipCount,
                    filter
                );
            }

            return mipmaps;
        }
    }

    std::vector<ITextureUtility::BakedMip> TextureUtility::bakeMips(
        std::span<const uint8_t> mip0Data,
        const Texture& texture)
    {
        const Texture::Description& description = texture.getDescription();
        uint32_t pixelSize = getPixelSizeByFormat(description.format);
        uint32_t arrayLayers = description.arrayLayers;

        std::vector<uint8_t> flippedMip0Data;
        if (texture.getFlipGreenChannel())
        {
            const bool has8BitGreen = description.format == PixelFormat::RG8_UNORM
                || description.format == PixelFormat::RGB8_UNORM
                || description.format == PixelFormat::RGBA8_UNORM;
            if (has8BitGreen)
            {
                flippedMip0Data.assign(mip0Data.begin(), mip0Data.end());
                for (size_t i = 1; i < flippedMip0Data.size(); i += pixelSize)
                    flippedMip0Data[i] = 255 - flippedMip0Data[i];
                mip0Data = flippedMip0Data;
            }
            else
            {
                LOG_WARN("[TextureUtility] Flip Green Channel only supports RG8/RGB8/RGBA8 UNORM sources, ignored");
            }
        }

        // mip0Data 只放 mip0 原始像素(Texture::setSourcePixels() 的约定), 其余各级 mip
        // 一律由这里生成
        std::vector<uint8_t> mipmapPixels = generateMipmapChain(mip0Data, texture);

        std::vector<BakedMip> result;
        result.reserve(description.mipLevels);
        uint32_t mipWidth = description.width;
        uint32_t mipHeight = description.height;
        uint32_t mipDepth = description.depth;

        rhi::TextureFormat format = texture.getRHITextureFormat();
        bool compFormat = isCompressionFormat(format);
        uint32_t blockSize = getBlockSizeByFormat(format);
        uint32_t pixelChannels = getPixelFormatChannels(description.format);
        bool hasAlpha = pixelFormatHasAlpha(description.format);
        PixelFormat pixelFormat = description.format;

        // 每个 mip×layer×depth slice 的源/目标位置互不重叠, 且都只读 mipmapPixels/mip0Data、
        // 只写自己独占的 result[i].data 子区间——先把这些位置算好收集成任务列表, 再统一并行执行
        std::vector<std::function<void()>> jobs;

        size_t srcMipOffset = 0;
        for (uint32_t i = 0; i < description.mipLevels; ++i)
        {
            size_t srcMipSize = mipWidth * mipHeight * mipDepth * pixelSize * arrayLayers;
            size_t mipDataSize = 0;

            std::span<const uint8_t> srcMipData;
            if (i == 0)
            {
                srcMipData = std::span<const uint8_t>(mip0Data.data(), srcMipSize);
            }
            else
            {
                srcMipData = std::span<const uint8_t>(mipmapPixels.data() + srcMipOffset, srcMipSize);
                srcMipOffset += srcMipSize;
            }

            uint32_t paddedWidth = (mipWidth + 3) & ~3;
            uint32_t paddedHeight = (mipHeight + 3) & ~3;

            if (compFormat)
                mipDataSize = (paddedWidth * paddedHeight) / 16 * blockSize * mipDepth * arrayLayers;
            else if (pixelChannels == 3)
                mipDataSize = srcMipSize / 3 * 4;
            else
                mipDataSize = srcMipSize;

            BakedMip& mip = result.emplace_back();
            mip.level = i;
            mip.data.resize(mipDataSize);
            uint8_t* dstBase = mip.data.data();

            if (compFormat)
            {
                uint32_t compressionChannels = getCompressionInputChannels(format);
                bool needsPadding = mipWidth != paddedWidth || mipHeight != paddedHeight || pixelChannels != compressionChannels;

                size_t rawSliceSize = static_cast<size_t>(mipWidth) * mipHeight * pixelSize;
                size_t dstSliceSize = (paddedWidth * paddedHeight) / 16 * blockSize;

                for (uint32_t layer = 0; layer < arrayLayers; ++layer)
                {
                    for (uint32_t z = 0; z < mipDepth; ++z)
                    {
                        size_t idx = layer * mipDepth + z;
                        std::span<const uint8_t> rawSlice = { srcMipData.data() + idx * rawSliceSize, rawSliceSize };
                        std::span<uint8_t> dstSlice = { dstBase + idx * dstSliceSize, dstSliceSize };

                        jobs.emplace_back(
                            [=]()
                            {
                                if (needsPadding)
                                {
                                    std::vector<uint8_t> paddedSlice = generatePaddingMipData(
                                        mipWidth, mipHeight, 1, 1,
                                        pixelFormat, compressionChannels,
                                        rawSlice
                                    );
                                    compressTextureData(paddedWidth, paddedHeight, paddedSlice, dstSlice, format, hasAlpha);
                                }
                                else
                                {
                                    compressTextureData(mipWidth, mipHeight, rawSlice, dstSlice, format, hasAlpha);
                                }
                            }
                        );
                    }
                }
            }
            else if (pixelChannels == 3)
            {
                // 当原数据通道数为3时, 补齐到4通道
                uint32_t dstPixelSize = pixelSize / 3 * 4;
                size_t sliceCount = static_cast<size_t>(mipWidth) * mipHeight;
                size_t rawSliceSize = sliceCount * pixelSize;
                size_t dstSliceSize = sliceCount * dstPixelSize;

                for (uint32_t layer = 0; layer < arrayLayers; ++layer)
                {
                    for (uint32_t z = 0; z < mipDepth; ++z)
                    {
                        size_t idx = layer * mipDepth + z;
                        const uint8_t* srcSlice = srcMipData.data() + idx * rawSliceSize;
                        uint8_t* dstSlice = dstBase + idx * dstSliceSize;

                        jobs.emplace_back(
                            [=]()
                            {
                                uint8_t opaqueBytes[4];
                                getOpaqueChannelBytes(pixelFormat, opaqueBytes);
                                uint32_t channelSize = pixelSize / 3;
                                for (size_t p = 0; p < sliceCount; ++p)
                                {
                                    uint8_t* dstPixel = dstSlice + p * dstPixelSize;
                                    std::memcpy(dstPixel, srcSlice + p * pixelSize, pixelSize);
                                    std::memcpy(dstPixel + pixelSize, opaqueBytes, channelSize);
                                }
                            }
                        );
                    }
                }
            }
            else
            {
                jobs.emplace_back(
                    [=]() { std::memcpy(dstBase, srcMipData.data(), srcMipData.size()); }
                );
            }

            mipWidth = std::max(mipWidth >> 1, 1u);
            mipHeight = std::max(mipHeight >> 1, 1u);
            mipDepth = std::max(mipDepth >> 1, 1u);
        }

        auto runJob = [](std::function<void()>& job) { job(); };
        if (jobs.size() > 1)
            std::for_each(std::execution::par, jobs.begin(), jobs.end(), runJob);
        else
            std::for_each(std::execution::seq, jobs.begin(), jobs.end(), runJob);

        return result;
    }
}
