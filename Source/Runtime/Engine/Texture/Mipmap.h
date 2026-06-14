#pragma once
#include <algorithm>

#include "Texture.h"

namespace hyp
{
    template <typename PixelType, typename DecodeFunc, typename EncodeFunc>
    void generateMipmap(
        uint32_t srcWidth,
        uint32_t srcHeight,
        uint32_t srcDepth,
        std::span<const PixelType> srcData,
        std::span<PixelType> dstData,
        DecodeFunc&& decode,
        EncodeFunc&& encode
    )
    {
        using DecodedType = std::invoke_result_t<DecodeFunc, PixelType>;
        const uint64_t expectedSize = static_cast<uint64_t>(srcWidth) * srcHeight * srcDepth;
        if (srcWidth == 0 || srcHeight == 0 || srcDepth == 0 || srcData.size() != expectedSize)
        {
            LOG_ERROR("Invalid texture dimensions or buffer size mismatch.");
            return;
        }

        const uint32_t dstWidth = std::max(1u, srcWidth / 2);
        const uint32_t dstHeight = std::max(1u, srcHeight / 2);
        const uint32_t dstDepth = std::max(1u, srcDepth / 2);
        assert(dstData.size() == dstWidth * dstHeight * dstDepth);

        const bool is2D = srcDepth == 1;
        const bool isFastPath = (srcWidth % 2 == 0) && (srcHeight % 2 == 0) && (is2D || srcDepth % 2 == 0);

        // ==========================================
        // Fast Path: 2x2 或 2x2x2 下采样
        // ==========================================
        if (isFastPath)
        {
            if (is2D)
            {
                // 2D Fast Path (Texture2D, TextureCube)
                for (uint32_t v = 0; v < dstHeight; ++v)
                {
                    const uint32_t srcRow0 = (v * 2) * srcWidth;
                    const uint32_t srcRow1 = srcRow0 + srcWidth;
                    const uint32_t dstRow = v * dstWidth;

                    for (uint32_t u = 0; u < dstWidth; ++u)
                    {
                        const uint32_t srcCol = u * 2;
                        DecodedType sum = decode(srcData[srcRow0 + srcCol]) + decode(srcData[srcRow0 + srcCol + 1]) +
                            decode(srcData[srcRow1 + srcCol]) + decode(srcData[srcRow1 + srcCol + 1]);
                        dstData[dstRow + u] = encode(sum * 0.25f);
                    }
                }
            }
            else
            {
                // 3D Fast Path (Texture3D)
                const uint32_t srcZStride = srcWidth * srcHeight;
                const uint32_t dstZStride = dstWidth * dstHeight;

                for (uint32_t w = 0; w < dstDepth; ++w)
                {
                    const uint32_t srcZ0 = (w * 2) * srcZStride;
                    const uint32_t srcZ1 = srcZ0 + srcZStride;
                    const uint32_t dstZ = w * dstZStride;

                    for (uint32_t v = 0; v < dstHeight; ++v)
                    {
                        const uint32_t srcY0 = (v * 2) * srcWidth;
                        const uint32_t srcY1 = srcY0 + srcWidth;
                        const uint32_t dstY = v * dstWidth;

                        for (uint32_t u = 0; u < dstWidth; ++u)
                        {
                            const uint32_t srcX = u * 2;
                            DecodedType sum =
                                decode(srcData[srcZ0 + srcY0 + srcX]) + decode(srcData[srcZ0 + srcY0 + srcX + 1]) +
                                decode(srcData[srcZ0 + srcY1 + srcX]) + decode(srcData[srcZ0 + srcY1 + srcX + 1]) +
                                decode(srcData[srcZ1 + srcY0 + srcX]) + decode(srcData[srcZ1 + srcY0 + srcX + 1]) +
                                decode(srcData[srcZ1 + srcY1 + srcX]) + decode(srcData[srcZ1 + srcY1 + srcX + 1]);

                            dstData[dstZ + dstY + u] = encode(sum * 0.125f);
                        }
                    }
                }
            }
        }

        // ==========================================
        // Slow Path: 带有奇数维度的面积/体积重采样积分
        // ==========================================
        auto runAreaWeightedIntegration = [&]<bool Is3D>() {
            const float scaleX = static_cast<float>(srcWidth) / dstWidth;
            const float scaleY = static_cast<float>(srcHeight) / dstHeight;
            const float scaleZ = Is3D ? (static_cast<float>(srcDepth) / dstDepth) : 1.0f;

            const uint32_t dstSliceSize = dstWidth * dstHeight;
            const uint32_t srcSliceSize = srcWidth * srcHeight;

            for (uint32_t w = 0; w < dstDepth; ++w) {
                const float z_min = w * scaleZ;
                const float z_max = Is3D ? ((w + 1) * scaleZ) : 1.0f;
                const uint32_t d_start = static_cast<uint32_t>(std::floor(z_min));
                const uint32_t d_end = Is3D ? std::min(srcDepth - 1, static_cast<uint32_t>(std::ceil(z_max)) - 1) : 0;

                for (uint32_t v = 0; v < dstHeight; ++v) {
                    const float y_min = v * scaleY;
                    const float y_max = (v + 1) * scaleY;
                    const uint32_t r_start = static_cast<uint32_t>(std::floor(y_min));
                    const uint32_t r_end = std::min(srcHeight - 1, static_cast<uint32_t>(std::ceil(y_max)) - 1);

                    for (uint32_t u = 0; u < dstWidth; ++u) {
                        const float x_min = u * scaleX;
                        const float x_max = (u + 1) * scaleX;
                        const uint32_t c_start = static_cast<uint32_t>(std::floor(x_min));
                        const uint32_t c_end = std::min(srcWidth - 1, static_cast<uint32_t>(std::ceil(x_max)) - 1);

                        DecodedType value_sum = { };
                        float weight_sum = 0.0f;

                        for (uint32_t z = d_start; z <= d_end; ++z) {
                            const float overlap_z = Is3D ? std::max(0.0f, std::min(z_max, static_cast<float>(z + 1)) - std::max(z_min, static_cast<float>(z))) : 1.0f;
                            if constexpr (Is3D) { if (overlap_z <= 0.0f) continue; }

                            const uint32_t z_offset = z * srcSliceSize;

                            for (uint32_t y = r_start; y <= r_end; ++y) {
                                const float overlap_y = std::max(0.0f, std::min(y_max, static_cast<float>(y + 1)) - std::max(y_min, static_cast<float>(y)));
                                if (overlap_y <= 0.0f) continue;

                                const uint32_t y_offset = z_offset + y * srcWidth;

                                for (uint32_t x = c_start; x <= c_end; ++x) {
                                    const float overlap_x = std::max(0.0f, std::min(x_max, static_cast<float>(x + 1)) - std::max(x_min, static_cast<float>(x)));

                                    float weight = overlap_x * overlap_y;
                                    if constexpr (Is3D) weight *= overlap_z;

                                    if (weight > 0.0f) {
                                        value_sum = value_sum + (decode(srcData[y_offset + x]) * weight);
                                        weight_sum += weight;
                                    }
                                }
                            }
                        }
                        dstData[w * dstSliceSize + v * dstWidth + u] = encode(value_sum * (1.0f / weight_sum));
                    }
                }
            }
        };

        if (is2D)
        {
            runAreaWeightedIntegration.template operator() < false > ();
        }
        else
        {
            runAreaWeightedIntegration.template operator() < true > ();
        }
    }

    class ColorSpace
    {
    public:
        static float srgbToLinear(uint8_t v);

        static uint8_t linearToSrgb(float v);
    };

    class FloatConverter
    {
    public:
        static uint16_t float32ToFloat16(float v);

        static float float16ToFloat32(uint16_t v);
    };

    template <PixelFormat F, bool isSRGB = false> struct PixelFormatTraits;

    template <> struct PixelFormatTraits<PixelFormat::R8_UNORM, true>
    {
        using Type = uint8_t;

        static auto getDecode()
        {
            return [](uint8_t v) -> float { return ColorSpace::srgbToLinear(v); };
        }

        static auto getEncode()
        {
            return [](float v) -> uint8_t { return ColorSpace::linearToSrgb(v); };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::R8_UNORM, false>
    {
        using Type = uint8_t;

        static auto getDecode()
        {
            return [](uint8_t v) -> float { return v / 255.0f; };
        }

        static auto getEncode()
        {
            return [](float v) -> uint8_t { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RG8_UNORM, true>
    {
        using Type = TVector2<uint8_t>;

        static auto getDecode()
        {
            return [](Type v) -> float2 { return float2(ColorSpace::srgbToLinear(v.x), ColorSpace::srgbToLinear(v.y)); };
        }

        static auto getEncode()
        {
            return [](float2 v) -> Type { return Type(ColorSpace::linearToSrgb(v.x), ColorSpace::linearToSrgb(v.y)); };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RG8_UNORM, false>
    {
        using Type = TVector2<uint8_t>;

        static auto getDecode()
        {
            return [](Type v) -> float2 { return float2(v.x / 255.0f, v.y / 255.0f); };
        }

        static auto getEncode()
        {
            return [](float2 v) -> Type {
                return Type(
                    static_cast<uint8_t>(std::clamp(v.x, 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<uint8_t>(std::clamp(v.y, 0.0f, 1.0f) * 255.0f + 0.5f)
                );
            };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RGB8_UNORM, true>
    {
        using Type = TVector3<uint8_t>;

        static auto getDecode()
        {
            return [](Type v) -> float3 { return float3(ColorSpace::srgbToLinear(v.x), ColorSpace::srgbToLinear(v.y), ColorSpace::srgbToLinear(v.z)); };
        }

        static auto getEncode()
        {
            return [](float3 v) -> Type { return Type(ColorSpace::linearToSrgb(v.x), ColorSpace::linearToSrgb(v.y), ColorSpace::linearToSrgb(v.z)); };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RGB8_UNORM, false>
    {
        using Type = TVector3<uint8_t>;

        static auto getDecode()
        {
            return [](Type v) -> float3 { return float3(v.x / 255.0f, v.y / 255.0f, v.z / 255.0f); };
        }

        static auto getEncode()
        {
            return [](float3 v) -> Type {
                return Type(
                    static_cast<uint8_t>(std::clamp(v.x, 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<uint8_t>(std::clamp(v.y, 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<uint8_t>(std::clamp(v.z, 0.0f, 1.0f) * 255.0f + 0.5f)
                );
            };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RGBA8_UNORM, true>
    {
        using Type = TVector4<uint8_t>;

        static auto getDecode()
        {
            return [](Type v) -> float4 {
                return float4(
                    ColorSpace::srgbToLinear(v.x),
                    ColorSpace::srgbToLinear(v.y),
                    ColorSpace::srgbToLinear(v.z),
                    ColorSpace::srgbToLinear(v.w)
                );
            };
        }

        static auto getEncode()
        {
            return [](float4 v) -> Type {
                return Type(
                    ColorSpace::linearToSrgb(v.x),
                    ColorSpace::linearToSrgb(v.y),
                    ColorSpace::linearToSrgb(v.z),
                    ColorSpace::linearToSrgb(v.w)
                );
            };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RGBA8_UNORM, false>
    {
        using Type = TVector4<uint8_t>;

        static auto getDecode()
        {
            return [](Type v) -> float4 { return float4(v.x / 255.0f, v.y / 255.0f, v.z / 255.0f, v.w / 255.0f); };
        }

        static auto getEncode()
        {
            return [](float4 v) -> Type {
                return Type(
                    static_cast<uint8_t>(std::clamp(v.x, 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<uint8_t>(std::clamp(v.y, 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<uint8_t>(std::clamp(v.z, 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<uint8_t>(std::clamp(v.w, 0.0f, 1.0f) * 255.0f + 0.5f)
                );
            };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::R16_UNORM, false>
    {
        using Type = uint16_t;

        static auto getDecode()
        {
            return [](uint16_t v) -> float { return v / 65535.0f; };
        }

        static auto getEncode()
        {
            return [](float v) -> uint16_t { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 65535.0f + 0.5f); };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RG16_UNORM, false>
    {
        using Type = TVector2<uint16_t>;

        static auto getDecode()
        {
            return [](Type v) -> float2 { return float2(v.x / 65535.0f, v.y / 65535.0f); };
        }

        static auto getEncode()
        {
            return [](float2 v) -> Type {
                return Type(
                    static_cast<uint8_t>(std::clamp(v.x, 0.0f, 1.0f) * 65535.0f + 0.5f),
                    static_cast<uint8_t>(std::clamp(v.y, 0.0f, 1.0f) * 65535.0f + 0.5f)
                );
            };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RGB16_UNORM, false>
    {
        using Type = TVector3<uint16_t>;

        static auto getDecode()
        {
            return [](Type v) -> float3 { return float3(v.x / 65535.0f, v.y / 65535.0f, v.z / 65535.0f); };
        }

        static auto getEncode()
        {
            return [](float3 v) -> Type {
                return Type(
                    static_cast<uint8_t>(std::clamp(v.x, 0.0f, 1.0f) * 65535.0f + 0.5f),
                    static_cast<uint8_t>(std::clamp(v.y, 0.0f, 1.0f) * 65535.0f + 0.5f),
                    static_cast<uint8_t>(std::clamp(v.z, 0.0f, 1.0f) * 65535.0f + 0.5f)
                );
            };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RGBA16_UNORM, false>
    {
        using Type = TVector4<uint16_t>;

        static auto getDecode()
        {
            return [](Type v) -> float4 { return float4(v.x / 65535.0f, v.y / 65535.0f, v.z / 65535.0f, v.w / 65535.0f); };
        }

        static auto getEncode()
        {
            return [](float4 v) -> Type {
                return Type(
                    static_cast<uint8_t>(std::clamp(v.x, 0.0f, 1.0f) * 65535.0f + 0.5f),
                    static_cast<uint8_t>(std::clamp(v.y, 0.0f, 1.0f) * 65535.0f + 0.5f),
                    static_cast<uint8_t>(std::clamp(v.z, 0.0f, 1.0f) * 65535.0f + 0.5f),
                    static_cast<uint8_t>(std::clamp(v.w, 0.0f, 1.0f) * 65535.0f + 0.5f)
                );
            };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::R16_SFLOAT, false>
    {
        using Type = uint16_t;

        static auto getDecode()
        {
            return [](uint16_t v) -> float { return FloatConverter::float16ToFloat32(v); };
        }

        static auto getEncode()
        {
            return [](float v) -> uint16_t { return FloatConverter::float32ToFloat16(v); };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RG16_SFLOAT, false>
    {
        using Type = TVector2<uint16_t>;

        static auto getDecode()
        {
            return [](Type v) -> float2 { return float2(FloatConverter::float16ToFloat32(v.x), FloatConverter::float16ToFloat32(v.y)); };
        }

        static auto getEncode()
        {
            return [](float2 v) -> Type { return Type(FloatConverter::float32ToFloat16(v.x), FloatConverter::float32ToFloat16(v.y)); };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RGB16_SFLOAT, false>
    {
        using Type = TVector3<uint16_t>;

        static auto getDecode()
        {
            return [](Type v) -> float3 {
                return float3(
                    FloatConverter::float16ToFloat32(v.x),
                    FloatConverter::float16ToFloat32(v.y),
                    FloatConverter::float16ToFloat32(v.z)
                );
            };
        }

        static auto getEncode()
        {
            return [](float3 v) -> Type {
                return Type(
                    FloatConverter::float32ToFloat16(v.x),
                    FloatConverter::float32ToFloat16(v.y),
                    FloatConverter::float32ToFloat16(v.z)
                );
            };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RGBA16_SFLOAT, false>
    {
        using Type = TVector4<uint16_t>;

        static auto getDecode()
        {
            return [](Type v) -> float4 {
                return float4(
                    FloatConverter::float16ToFloat32(v.x),
                    FloatConverter::float16ToFloat32(v.y),
                    FloatConverter::float16ToFloat32(v.z),
                    FloatConverter::float16ToFloat32(v.w)
                );
            };
        }

        static auto getEncode()
        {
            return [](float4 v) -> Type {
                return Type(
                    FloatConverter::float32ToFloat16(v.x),
                    FloatConverter::float32ToFloat16(v.y),
                    FloatConverter::float32ToFloat16(v.z),
                    FloatConverter::float32ToFloat16(v.w)
                );
            };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::R32_SFLOAT, false>
    {
        using Type = float;

        static auto getDecode()
        {
            return [](float v) -> float { return v; };
        }

        static auto getEncode()
        {
            return [](float v) -> float { return v; };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RG32_SFLOAT, false>
    {
        using Type = float2;

        static auto getDecode()
        {
            return [](float2 v) -> float2 { return v; };
        }

        static auto getEncode()
        {
            return [](float2 v) -> float2 { return v; };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RGB32_SFLOAT, false>
    {
        using Type = float3;

        static auto getDecode()
        {
            return [](float3 v) -> float3 { return v; };
        }

        static auto getEncode()
        {
            return [](float3 v) -> float3 { return v; };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RGBA32_SFLOAT, false>
    {
        using Type = float4;

        static auto getDecode()
        {
            return [](float4 v) -> float4 { return v; };
        }

        static auto getEncode()
        {
            return [](float4 v) -> float4 { return v; };
        }
    };

    template <PixelFormat F, bool isSRGB>
    void invokeGenerate(uint32_t srcWidth, uint32_t srcHeight, uint32_t srcDepth, std::span<const uint8_t> src, std::span<uint8_t> dst)
    {
        using Traits = PixelFormatTraits<F, isSRGB>;
        using T = typename Traits::Type;

        auto typedSrc = std::span<const T>(reinterpret_cast<const T*>(src.data()), src.size() / sizeof(T));
        auto typedDst = std::span<T>(reinterpret_cast<T*>(dst.data()), dst.size() / sizeof(T));

        generateMipmap<T>(
            srcWidth, srcHeight, srcDepth,
            typedSrc, typedDst,
            Traits::getDecode(),
            Traits::getEncode()
        );
    }
}
